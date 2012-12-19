#!/usr/bin/env ruby
#encoding: utf-8


$LOAD_PATH.unshift File.expand_path(File.dirname(__FILE__))

require 'parser'


parser = Parser.new # "/Users/vasfed/work/heap_dump/tmp/tcs-ruby_1_9_3"



#RVALUE + rb_objspace, gc_list are used directly

# heaps_slot / heaps_header / sorted_heaps_slot?
# gc_list
# p tree; exit

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

# typed data:

#TODO: not only search, rb_data_type_t, but also rb_data_type_struct ?

class TypedDataEntry
  attr_reader :name
  attr_reader :type_struct

  attr_reader :mark_func

  def initialize name
    @name = name
    # @typename = typename
  end

  def handle_mark_func func_name
    @mark_func = find_function(tree, mark_func_name)

    input_param_name = @mark_func.type.params.first.name

    # find assignment with cast
    @mark_func.preorder{|n|
      if n.is_a?(C::Declaration)
        if n.declarators && n.declarators.any?{|d| d.indirect_type.is_a?(C::Pointer) && d.init.is_a?(C::Variable) && d.init.name == input_param_name}
          @type_struct = n.type.name
          puts "// Found type: #{type_struct}"
          return true
        else
          throw :prune
        end
      end
    }

    #FIXME: if cast is hack-styled - there will be no assignment, but type in param

    return :no_type
  end

  def handle_alloc_func tree
    # no mark -> no references, good, but still we may want the type for dumping reasons
    # this is longer - have to check all functions for a call to rb_data_typed_object_alloc
    puts "// no type in mark func for #{typename}. searching for alloc"

    tree.entities.each{|entity|
      next unless entity.is_a?(C::FunctionDef) && entity.def
      entity.def.preorder{|n|
        if n.is_a?(C::Call) && n.expr.name == 'rb_data_typed_object_alloc' && n.args.last.is_a?(C::Address) && n.args.last.expr.name == var_name
          puts "//found alloc call, var is #{n.args[1].name}"
          # p entity
          # puts entity
          var = n.args[1].name
          type_struct = tree.base_type_name tree.find_type_of_var(var, entity, n)
          #FIXME: unnamed types...
          puts "// type is #{type_struct}"
          return
        end
      }
    }
  end

end


types = []

#FIXME: formatting may interfere
`grep -l 'static const rb_data_type_t' #{$ruby_src}/*.c`.split.each do|filename|
  puts "// #{filename}"
  #FIXME:
  next unless filename == '/Users/vasfed/.rvm/src/ruby-1.9.3-p194/time.c'

  tree = parser.parse File.expand_path(filename)

  nodes = tree.entities.find_all{|n|
    n.is_a?(C::Declaration) && n.storage == :static && n.type.name == 'rb_data_type_t' && n.declarators.any?{|d| d.init }
  }.each{|decl|
    decl.declarators.each{|d|
      var_name = d.name
      typename = d.init.member_inits.first.init.val
      mark_func_name = d.init.member_inits[1].init.member_inits.first.init.name
      puts "// typename: #{var_name} contains definition for '#{typename}': #{mark_func_name}"

      type_entry = TypedDataEntry.new typename
      types << type_entry

      # now grab marking function :)
      type_struct = nil
      mark_func = nil
      if mark_func_name
        type_entry.handle_mark_func mark_func_name
      else
        puts "// no mark func for #{typename}."
      end

      unless type_entry.type_struct
        type_entry.handle_alloc_func tree
      end

      # add our findings to src...
      unless !type_entry.type_struct || parser.known_typename?(type_entry.type_struct)
        if s = tree.find_type(type_struct)
          add_src = []
          res = tree.type_dependencies tree, s, add_src
          #?
          # add_src << s.to_s
          puts add_src.join("\n")
        else
          puts "// cannot find unknown struct #{type_struct} !"
        end
      end

      puts type_entry.mark_func if type_entry.mark_func

    }
  }

  # p tree if ENV['PTREE']

  #FIXME:
  # break
  puts "\n\n\n"
end



