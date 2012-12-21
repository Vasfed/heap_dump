
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
    named_data_types << type_entry

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
    type_entry
  end

  def handle_named_data_types_from filename
    puts "// #{filename}"
    tree = parser.parse File.expand_path(filename)
    @current_file = tree

    res = []

    nodes = tree.entities.find_all{|n|
      n.is_a?(C::Declaration) && n.storage == :static && n.type.name == 'rb_data_type_t' && n.declarators.any?{|d| d.init }
    }.each{|decl|
      decl.declarators.each{|d|
        var_name = d.name
        typename = d.init.member_inits.first.init.val
        mark_func_name = d.init.member_inits[1].init.member_inits.first.init.name
        puts "// typename: #{var_name} contains definition for '#{typename}': #{mark_func_name}"
        res << handle_named_data_type(tree, typename, var_name, mark_func_name)
      }
    }

    p tree if ENV['PTREE']

    puts "// making dumpers..."
    res.each{|t|
      func = make_named_data_type_dumper t
    }

    puts "\n\n\n"
  end

  def named_data_types
    @named_data_types ||= []
  end

  def c_safe str
    str.gsub(/[^a-zA-Z_0-9]/, "_")
  end

  def find_std_ruby_func name
    # priority to current file
    if @current_file && (f = @current_file.find_function(name))
      return f
    end
    %w{ gc.c vm.c }.each{|file|
      if f = parsed_ruby_file(file).find_function(name)
        return f
      end
    }
    return nil
  end


  def make_dumper_from_marker func, add_to_src=true
    # p func
    #TODO: merge intercepted gc_marks with info from struct...
    if ready_dumpers[func.name]
      puts "// already known #{func.name}"
      return func
    end
    ready_dumpers[func.name] = true

    calls = []
    func.preorder{|n|
      if n.is_a?(C::Call)
        calls << n
        throw :prune
      end
    }

    func.storage = :static
    func.name.gsub! /mark/, 'dump'
    func.type.params << C::Parameter.parse("struct dump_ctx_t* ctx") if add_to_src


    calls.each{|c|
      raise "Unsupported function call" unless c.expr.is_a?(C::Variable)

      case c.expr.name
      when "rb_gc_mark"
        c.expr.name = "dump_single"
        c.args << C::StringLiteral.new(c.args.to_s)
        c.args << C::Variable.new("ctx")
      when "gc_mark"
        c.expr.name = "dump_single"
        c.args.first.detach
        c.args << C::StringLiteral.new(c.args.to_s)
        c.args << C::Variable.new("ctx")
      when "rb_gc_mark_locations"
        c.expr.name = "dump_locations"
        c.args << C::StringLiteral.new(c.args.first.to_s)
        c.args << C::Variable.new("ctx")
      when "st_foreach" # also sa_foreach etc. for tcs
        # the hard one - need to pass context
        # raise 'st_foreach not implemented yet'
        # struct mark_tbl_arg arg;
        # st_foreach(tbl, mark_entry, (st_data_t)&arg);
        # Call
          # expr: Variable
          #     name: "st_foreach"
          # args: 
          #     - Arrow
          #         expr: Variable
          #             name: "vm"
          #         member: Member
          #             name: "living_threads"
          #     - Variable
          #         name: "vm_mark_each_thread_func"
          #     - IntLiteral
          #         val: 0
        if c.args[1].is_a?(C::Variable) && (f = find_std_ruby_func(c.args[1].name))
          c.swap_with(replacement = C::Statement.parse("{ st_data_t_wrapper_var.data = ___stdata__marker___; }"))
          replacement.stmts.first.expr.rval = c.args.last
          replacement.stmts << c
          c.args[2] = C::Expression.parse("(st_data_t)&st_data_t_wrapper_var")
          func.def.stmts.insert(0, wrapper_def = C::Declaration.parse(
            "struct{ st_data_t ctx; st_data_t data; } st_data_t_wrapper_var = { (st_data_t)ctx, 0};",
            @current_file.parser))
          make_dumper_from_marker f, false
          # f.args.last.detach # remove appended ctx
          p f

          param = f.type.params.last
          param.detach
          f.type.params << C::Parameter.parse("st_data_t st_data_t_wrapper_p", @current_file.parser)
          f.def.stmts.insert(0, C::Declaration.parse(
            "struct{ st_data_t ctx; st_data_t data; } *st_data_t_wrapper_var = (void*)st_data_t_wrapper_p;",
            @current_file.parser))
          decl = C::Declaration.new
          decl.type = param.type
          d = C::Declarator.new
          d.name = param.name
          d.init = C::Arrow.parse("st_data_t_wrapper_var->data")
          decl.declarators << d
          f.def.stmts.insert(1, decl)
          f.def.stmts.insert(2, C::Declaration.parse("struct dump_ctx_t* ctx = (struct dump_ctx_t*)st_data_t_wrapper_var->ctx;",
            @current_file.parser))

          #TODO: replace dump_single in iterator, also make hashes/arrays
          add_src f.to_s
        else
          raise "Unsupported st_foreach call found in #{func.name}"
        end

      when "rb_mark_generic_ivar"
        #TODO


      # when "gc_mark_children"

# rb_mark_set
# rb_mark_hash
# rb_mark_tbl
# rb_mark_method_entry
# st_foreach
# mark_event_hooks
      when 'printf'
        c.detach
      when "rb_check_typeddata" #skip
      else
        func_name = c.expr.name.to_s
        if func_name =~ /mark/ && !ready_dumpers[func_name]
          if f = find_std_ruby_func(func_name)
            puts "// making dumper for #{func_name}"
            ready_dumpers[func_name] = true
            make_dumper_from_marker f
          else
            puts "// Cannot find #{c.expr}, already known: #{@ready_dumpers.keys}"
            raise "Cannot find function #{c.expr}"
          end
        else
          puts "// already known #{c.expr}"
        end
        c.expr.name.gsub! /mark/, 'dump'
        c.args << C::Variable.new("ctx")
      end
    }

    add_src func.to_s if add_to_src
    func
  end

  def ready_dumpers
    @ready_dumpers ||= {}
  end

  def make_named_data_type_dumper type
    if type.mark_func
      make_dumper_from_marker type.mark_func
    else
      # no marker => no references, but there still may be something interesting...
    end
  end


  def handle_named_data_types_common
    #TODO: generate lists etc...
    named_data_types.each{|t|
      #...
    }
  end

  def handle_named_data_types
    #FIXME: formatting may interfere
    `grep -l 'static const rb_data_type_t' #{ruby_src_dir}/*.c`.split.each do|filename|
      handle_named_data_types_from(filename)
    end

    handle_named_data_types_common
  end
end
