
# typed data:

#TODO: not only search, rb_data_type_t, but also rb_data_type_struct ?

class TypedDataEntry
  attr_reader :name
  attr_reader :type_struct

  attr_reader :mark_func
  attr_reader :var_name

  def initialize name, var_name
    @name = name
    @var_name = var_name
    # @typename = typename
  end

  def handle_mark_func func_name, tree
    @mark_func = tree.find_function(func_name)

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
    puts "// no type in mark func for #{name}. searching for alloc"

    tree.entities.each{|entity|
      next unless entity.is_a?(C::FunctionDef) && entity.def
      entity.def.preorder{|n|
        if n.is_a?(C::Call) && n.expr.name == 'rb_data_typed_object_alloc' && n.args.last.is_a?(C::Address) && n.args.last.expr.name == self.var_name
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

  def add_to_gen tree, generator
    if type_struct && s = tree.find_type(type_struct)
      res = generator.type_dependencies tree, s
    else
      puts "// cannot find struct #{type_struct} for #{name}!"
    end
  end

end




module GeneratorTypedDataStuff


  def handle_named_data_type tree, typename, var_name, mark_func_name
    type_entry = TypedDataEntry.new typename, var_name
    @named_data_types << type_entry

    # now grab marking function :)
    type_struct = nil
    mark_func = nil
    if mark_func_name
      type_entry.handle_mark_func mark_func_name, tree
    else
      puts "// no mark func for #{typename}."
    end

    unless type_entry.type_struct
      type_entry.handle_alloc_func tree
    end

    # add our findings to src...
    type_entry.add_to_gen tree, self unless known_typename?(type_entry.type_struct)

    # TODO: mark funcs....
    add_src type_entry.mark_func if type_entry.mark_func
  end

  def handle_named_data_types_from filename
    puts "// #{filename}"
    tree = parser.parse File.expand_path(filename)

    nodes = tree.entities.find_all{|n|
      n.is_a?(C::Declaration) && n.storage == :static && n.type.name == 'rb_data_type_t' && n.declarators.any?{|d| d.init }
    }.each{|decl|
      decl.declarators.each{|d|
        var_name = d.name
        typename = d.init.member_inits.first.init.val
        mark_func_name = d.init.member_inits[1].init.member_inits.first.init.name
        puts "// typename: #{var_name} contains definition for '#{typename}': #{mark_func_name}"
        handle_named_data_type tree, typename, var_name, mark_func_name
        #TODO: size func => measure sizes?
      }
    }

    p tree if ENV['PTREE']
    puts "\n\n\n"
  end

  def handle_named_data_types
    @named_data_types ||= []

    #FIXME:
    # return handle_named_data_types_from '/Users/vasfed/.rvm/src/ruby-1.9.3-p194/encoding.c'

    #FIXME: formatting may interfere
    `grep -l 'static const rb_data_type_t' #{ruby_src_dir}/*.c`.split.each do|filename|
      handle_named_data_types_from filename
    end

    #TODO: generate lists etc...
    @named_data_types.each{|t|
      #...
    }
  end
end
