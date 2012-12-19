#encoding: utf-8

require 'cast'
require 'rbconfig'



class Parser

  attr_reader :ruby_src_dir

  def initialize ruby_dir=find_ruby_src
    @ruby_src_dir = ruby_dir
  end

  def find_ruby_src
    bindir = RbConfig::CONFIG['bindir']
    if bindir =~ %r{(^.*/\.rbenv/versions)/([^/]+)/bin$}
      ruby_include = "#{$1}/#{$2}/include/ruby-1.9.1/ruby-#{$2}"
      ARGV << "--with-ruby-include=#{ruby_include}"
    elsif bindir =~ %r{(^.*/\.rvm/rubies)/([^/]+)/bin$}
      ruby_include = "#{$1}/#{$2}/include/ruby-1.9.1/#{$2}"
      ruby_include = "#{ENV['rvm_path']}/src/#{$2}" unless File.exist?(ruby_include)
      ARGV << "--with-ruby-include=#{ruby_include}"
    end
  end

  def prepare_source src
    # need patched cast (with support for string concatenation and source map dropping)
    #+ cast does not support __asm, __extension__  etc. => patch them out via defines...
    @cpp ||= begin
      cpp = C::Preprocessor.new
      cpp.macros.merge!(
        '__asm(...)' => '',
        '__asm__(...)' => 'rb_bug("heap_dump generator: asm code not supported, found in " __FILE__ " line " __LINE__)',
        __inline:'',
        __inline__:'',
        __extension__:'', #??
        '__attribute__(x)' => ''
        )
      cpp.undef_macros << '__BLOCKS__'
      cpp.undef_macros << '__GNUC__' # dangerous, but...

      cpp.include_path << self.ruby_src_dir
      cpp.include_path << self.ruby_src_dir + '/include'
      cpp.include_path << RbConfig::CONFIG['includedir']
      cpp.include_path << "#{RbConfig::CONFIG['rubyhdrdir']}/#{RbConfig::CONFIG['arch']}"
      # cpp.macros[:@include] = 'antigcc.h'
      cpp
    end
    @cpp.preprocess_file(src, force:true)
  end

  # parser_with_source
  def parse src
    parser = C::Parser.new

    parser.pos.filename = src
    parser.type_names << '__builtin_va_list'
    tree = parser.parse(prepare_source(src))
    Tree.new src, tree, parser
  end

  def known_typenames
    @known_typenames ||= fetch_known_typenames
  end

  # check if typename is available to gem
  def known_typename? type
    # return false unless @known_typenames
    known_typenames.include? type
  end

  # typenames that are available to gems without hacking
  def fetch_known_typenames
    #TODO: generate this source from heapdump itself:
    tree = parse('dummy_gem.c')

    types = tree.parser.type_names
    tree.entities.each{|n|
      #TODO: does this include typedefs to known types?
      if n.is_a?(C::Declaration) && n.type.name && n.type.members
        types << n.type.name
      end
    }
    types
  end


  class Tree
    attr_reader :tree, :parser
    attr_reader :filename
    def initialize src_file, tree, parser
      @filename = src_file
      @tree = tree
      @parser = parser
    end

    def inspect
      "#{self.class}<#{filename}>"
    end

    def entities
      @tree.entities
    end

    def find_struct name
      res = tree.entities.find{|n|
        n.is_a?(C::Declaration) && n.type.is_a?(C::Struct) && n.type.members &&
          (n.type.name == name || n.storage == :typedef && n.declarators && n.declarators.find{|d| d.name == name})
      } #|| tree.entities.find{|n|
      #   n.is_a?(C::Struct) && n.type.is_a?(C::Struct) && n.type.members
      # }
    end

    def find_type name
      tree.entities.find{|n|
       n.is_a?(C::Declaration) && (n.type.is_a?(C::Type) && n.type.name == name && n.type.members ||
        n.storage == :typedef && n.declarators && n.declarators.find{|d| d.name == name})
      } || tree.entities.find{|n|
       n.is_a?(C::Declaration) && (n.type.is_a?(C::Type) && n.type.name == name || #&& n.type.members ||
        n.storage == :typedef && n.declarators && n.declarators.find{|d| d.name == name})
      }
    end

    def find_function name
      tree.entities.find{|n| n.is_a?(C::FunctionDef) && n.name == name && n.def }
    end

    #REFACTOR: remove tree_unused
    def type_dependencies tree_unused, item, add_src = [], res = Set.new
      new_types = Set.new

      item.postorder{|n|
        if n.is_a?(C::DirectType) # indirect have direct on leafs
          next unless n.name
          #FIXME: name collisions vs scopes!
          new_types << n.name unless res.include?(n.name)
          if n.name && !n.members
            new_types << n.name
          end
        end
      }

      new_types.each{|typename|
        next if res.include?(typename) || $known_typenames && $known_typenames.include?(typename)
        res << typename
        # puts "Descending to #{typename}"
        type = find_type tree, typename
        unless type
          puts "// cannot find definition for #{typename}"
          next
        end
        type_dependencies tree, type, add_src, res
        add_src << type.to_s
      }
      res
    end

    def base_type_name type
      return type unless type
      raise "Not type" unless type.is_a?(C::Type)
      if type.is_a?(C::IndirectType)
        return base_type_name(type.type)
      elsif type.is_a? C::DirectType
        return type.name
      end
      puts "// primitive type"
      #FIXME: ???
      return type.to_s
    end

    def walk_up_tree node, stop
      while node && node != stop
        yield node
        node = node.parent
      end
    end

    # walks down the tree, returns (somewhat of base type, not including indirect...)
    def find_type_of_var_down var, node
      # check params:
      if node.is_a?(C::FunctionDef) && node.type.params
        node.type.params.each{|param|
          return param.type if param.name == var
        }
      end
      # local var.. actually local vars can overload params
      node.preorder {|n|
        # Todo: fors etc.?
        if n.is_a?(C::Declaration) && n.declarators && n.declarators.any?{|d| d.name == var }
          return n.type
        end
      }
      nil
    end

    # walks up tree for local var type(this handles overloading)
    def find_type_of_var var, func_tree, location_node=nil
      if location_node
        prev_loc = location_node
        walk_up_tree(location_node, func_tree){|n|
          if n.is_a?(C::Block) || n.is_a?(C::FunctionDef)
            t = find_type_of_var_down var, n
            return t if t
          end
        }
      end
      find_type_of_var_down var, func_tree
    end

  end

end