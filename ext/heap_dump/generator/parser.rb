#encoding: utf-8

require 'cast'
require 'rbconfig'



class Parser

  attr_reader :ruby_src_dir

  def initialize ruby_dir
    @ruby_src_dir = ruby_dir
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
    Tree.new src, tree, parser, self
  end


  class Tree
    attr_reader :tree, :parser
    attr_reader :filename
    attr_reader :top_parser
    def initialize src_file, tree, parser, top_parser
      @filename = src_file
      @tree = tree
      @parser = parser
      @top_parser = top_parser
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