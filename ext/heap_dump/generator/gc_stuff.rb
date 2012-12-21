#encoding: utf-8

module GCStuff
  def fetch_rb_objspace tree
    rb_objspace = tree.find_struct 'rb_objspace'
    if !rb_objspace
      rb_objspace = tree.find_struct 'rb_objspace_t'

      if !rb_objspace
        puts "#error Cannot find objspace struct!"
      end
    end

    type_dependencies tree, rb_objspace
    #src includes objspace too

    #TODO: defines for objspace from gc.c
    rb_objspace
  end

  def make_for_each_heap_slot tree
    count_objects = tree.find_function 'count_objects'

    fors = count_objects.def.stmts.select{|n| n.is_a? C::For }
    raise "function count_objects does not match" unless fors.size == 3

    count_for = fors[1]

    internal_fors = count_for.stmt.stmts.select{|n| n.is_a?(C::For)}
    raise "function count_objects does not match" unless internal_fors.size == 1
    internal_fors.first.stmt = C::Statement.parse("{ f; }")

    type_dependencies tree, count_for

    add_src "#define FOR_EACH_HEAP_SLOT(f) " + count_for.to_s.strip.gsub(/\n/s, "\\\n")
  end

  def fetch_is_pointer_to_heap tree
    func = tree.find_function 'is_pointer_to_heap'
    add_src func.to_s
  end

  def handle_gc_stuff
    gc_tree = parsed_ruby_file 'gc.c'

    fetch_rb_objspace gc_tree
    make_for_each_heap_slot gc_tree
    fetch_is_pointer_to_heap gc_tree
  end
end
