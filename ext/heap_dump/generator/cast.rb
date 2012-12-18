#!/usr/bin/env ruby
#encoding: utf-8

require 'cast'
require 'rbconfig'

  bindir = RbConfig::CONFIG['bindir']
  if bindir =~ %r{(^.*/\.rbenv/versions)/([^/]+)/bin$}
    ruby_include = "#{$1}/#{$2}/include/ruby-1.9.1/ruby-#{$2}"
    ARGV << "--with-ruby-include=#{ruby_include}"
  elsif bindir =~ %r{(^.*/\.rvm/rubies)/([^/]+)/bin$}
    ruby_include = "#{$1}/#{$2}/include/ruby-1.9.1/#{$2}"
    ruby_include = "#{ENV['rvm_path']}/src/#{$2}" unless File.exist?(ruby_include)
    ARGV << "--with-ruby-include=#{ruby_include}"
  end
  puts "Using ruby source from #{ruby_include}"


$ruby_src =ruby_include

# $ruby_src = "/Users/vasfed/work/heap_dump/tmp/tcs-ruby_1_9_3"


def prepare_source src
  # need patched cast (with support for string concatenation and source map dropping)

  #+ cast does not support __asm, __extension__  etc. => antigcc.h

  @cpp ||= begin
    cpp = C::Preprocessor.new
    cpp.macros.merge!(
      '__asm(...)' => '',
      '__asm__(...)' => 'rb_bug("heap_dump generator: asm code not supported")',
      __inline:'',
      __inline__:'',
      __extension__:'', #??
      '__attribute__(x)' => ''
      )
    cpp.undef_macros << '__BLOCKS__'
    cpp.undef_macros << '__GNUC__' # dangerous, but...

    cpp.include_path << $ruby_src
    cpp.include_path << $ruby_src + '/include'
    cpp.include_path << RbConfig::CONFIG['includedir']
    cpp.include_path << "#{RbConfig::CONFIG['rubyhdrdir']}/#{RbConfig::CONFIG['arch']}"

    # cpp.macros[:@include] = 'antigcc.h'
    cpp
  end

  #`gcc -x c -I"#{$ruby_src}" -I"#{$ruby_src}/include" -imacros antigcc.h -E -fno-common "#{src}"`
  # puts "Running #{@cpp.send(:full_command, src)}"
  @cpp.preprocess_file(src, force:true)
end

def parser_with_source src
  parser = C::Parser.new

  parser.pos.filename = src
  parser.type_names << '__builtin_va_list'
  tree = parser.parse(prepare_source(src))
  [tree, parser]
end

def fetch_known_typenames
  tree,parser = parser_with_source('dummy_gem.c')

  types = parser.type_names
  tree.entities.each{|n|
    if n.is_a?(C::Declaration) && n.type.name && n.type.members
      types << n.type.name
    end
  }
  $known_typenames = types
  types
end

def find_struct tree, name
  res = tree.entities.find{|n|
    n.is_a?(C::Declaration) && n.type.is_a?(C::Struct) && n.type.members &&
      (n.type.name == name || n.storage == :typedef && n.declarators && n.declarators.find{|d| d.name == name})
  } #|| tree.entities.find{|n|
  #   n.is_a?(C::Struct) && n.type.is_a?(C::Struct) && n.type.members
  # }
end

def find_type tree, name
  tree.entities.find{|n|
   n.is_a?(C::Declaration) && (n.type.is_a?(C::Type) && n.type.name == name && n.type.members ||
    n.storage == :typedef && n.declarators && n.declarators.find{|d| d.name == name})
  } || tree.entities.find{|n|
   n.is_a?(C::Declaration) && (n.type.is_a?(C::Type) && n.type.name == name || #&& n.type.members ||
    n.storage == :typedef && n.declarators && n.declarators.find{|d| d.name == name})
  }
end

def find_function tree, name
  tree.entities.find{|n|
    n.is_a?(C::FunctionDef) && n.name == name && n.def
  }
end

def type_dependencies tree, item, add_src = [], res = Set.new
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

def walk_up_tree node, stop
  while node && node != stop
    yield node
    node = node.parent
  end
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
  #???
  return type.to_s
end

# walks down the tree, returns (somewhat of base type, not including indirect...)
def find_type_of_var_down var, node
  # check params:
  # puts "//params.."
  if node.is_a?(C::FunctionDef) && node.type.params
    node.type.params.each{|param|
      if param.name == var
        return param.type
      end
    }
  end

  # puts "// searching local vars..."
  # local var..
  node.preorder {|n|
    # Todo: fors etc.?
    if n.is_a?(C::Declaration) && n.declarators && n.declarators.any?{|d| d.name == var }
      return n.type
    end
    # if n.is_a?(C::Declarator) && n.name == var
    #   puts "// found declarator"
    #   p n
    #   #indirect - Function Pointer Array
    #   if n.indirect_type
    #     #FIXME: pointers?...
    #     return n.parent.type
    #   else
    #     return n.parent.type
    #   end
    # end
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


#RVALUE + rb_objspace, gc_list are used directly

# heaps_slot / heaps_header / sorted_heaps_slot?
# gc_list
# p tree; exit



$known_typenames = fetch_known_typenames.to_a.join("\n")

if false
# puts "RBasic Known(shoud be true): #{$known_typenames.include?('RBasic').inspect}"

tree,parser = parser_with_source(File.join($ruby_src, 'gc.c'))

# p parser.type_names

begin
  #TODO: exclude types from ruby.h and other available includes
  rb_objspace = find_struct tree, 'rb_objspace'
  if !rb_objspace
    puts "#error Cannot find objspace struct!"
    # p tree
    require 'pry'
    binding.pry
    rb_objspace = find_struct tree, 'rb_objspace_t'
  end
  src = []
  dependencies = type_dependencies tree, rb_objspace, src
  #src includes objspace too

  #TODO: agregate dependencies
  puts src.join("\n\n")
  $objspace = rb_objspace
end

# exit #!!


# FOR_EACH_HEAP_SLOT:

begin
  count_objects = find_function tree, 'count_objects'

  fors = count_objects.def.stmts.select{|n| n.is_a? C::For }
  raise "function count_objects does not match" unless fors.size == 3

  count_for = fors[1]

  internal_fors = count_for.stmt.stmts.select{|n| n.is_a?(C::For)}
  raise "function count_objects does not match" unless internal_fors.size == 1
  internal_fors.first.stmt = C::Statement.parse("{ f; }")

  src = []
  type_dependencies tree, count_for, src, dependencies
  puts src

  str = "#define FOR_EACH_HEAP_SLOT(f) " + count_for.to_s.strip.gsub(/\n/s, "\\\n")
  puts str
end

begin
  puts
  func = find_function tree, 'is_pointer_to_heap'
  puts func
end

end

#TODO: get other structs and funcs automatically
#TODO: grep for typed data type structs => even more autogen

# grep static const rb_data_type_t 



# typed data:

#TODO: also rb_data_type_struct ?

# - Declaration
#     storage: extern
#     type: CustomType (const)
#         name: "rb_data_type_t"
#     declarators: 
#         - Declarator
#             name: "ruby_threadptr_data_type"


`grep -l 'static const rb_data_type_t' #{$ruby_src}/*.c`.split.each do|filename|
  puts "// #{filename}"
  #FIXME:
  # next unless filename == '/Users/vasfed/.rvm/src/ruby-1.9.3-p194/time.c'

  tree,parser = parser_with_source(File.expand_path(filename))

  nodes = tree.entities.find_all{|n|
    n.is_a?(C::Declaration) && n.storage == :static && n.type.name == 'rb_data_type_t' && n.declarators.any?{|d| d.init }
  }.each{|decl|
    decl.declarators.each{|d|
      var_name = d.name
      typename = d.init.member_inits.first.init.val
      mark_func_name = d.init.member_inits[1].init.member_inits.first.init.name
      puts "// typename: #{var_name} contains definition for '#{typename}': #{mark_func_name}"

      # now grab marking function :)
      type_struct = nil
      mark_func = nil
      if mark_func_name
        mark_func = find_function(tree, mark_func_name)

        #FIXME: if cast is hack-styled - there will be no assignment
        input_param_name = mark_func.type.params.first.name
        # puts "input param name is #{input_param_name}"

        catch :stop do
          mark_func.preorder{|n|
            if n.is_a?(C::Declaration)
              if n.declarators && n.declarators.any?{|d| d.indirect_type.is_a?(C::Pointer) && d.init.is_a?(C::Variable) && d.init.name == input_param_name}
                type_struct = n.type.name
                puts "// Found type: #{type_struct}"
                throw :stop
              else
                throw :prune
              end
            end
          }
        end

        #TODO: DRY
        s = find_type(tree, type_struct)
        # p s
        add_src = []
        res = type_dependencies tree, s, add_src
        puts add_src.join("\n")

        puts mark_func
      else
        puts "// no mark func for #{typename}."
      end

      unless type_struct
        # no mark -> no references, good, but still we may want the type for dumping reasons
        # this is longer - have to check all functions for a call to rb_data_typed_object_alloc
        puts "// no type in mark func for #{typename}. searching for alloc"

        # ne = find_function(tree, 'enc_new')
        # p ne

        tree.entities.each{|entity|
          next unless entity.is_a?(C::FunctionDef) && entity.def
          catch :stop do
            entity.def.preorder{|n|
              if n.is_a?(C::Call) && n.expr.name == 'rb_data_typed_object_alloc' && n.args.last.is_a?(C::Address) && n.args.last.expr.name == var_name
                puts "//found alloc call, var is #{n.args[1].name}"
                # p entity
                # puts entity
                var = n.args[1].name
                type_struct = base_type_name find_type_of_var(var, entity, n)
                #FIXME: unnamed types...
                puts "// type is #{type_struct}"
                unless !type_struct || $known_typenames && $known_typenames.include?(type_struct)
                  s = find_type(tree, type_struct)
                  if s
                    add_src = []
                    res = type_dependencies tree, s, add_src
                    add_src << s.to_s
                    puts add_src.join("\n")
                  else
                    puts "// cannot find unknown struct #{type_struct} !"
                  end
                end
                throw :stop
              end
            }
          end
        }
      end

    }
  }

  p tree if ENV['PTREE']

  #FIXME:
  # break
  puts "\n\n\n"
end



