# HeapDump

Low-level ruby heap memory dump - including data and code references, useful for finding leaks.
Has no performance overhead while not active, so can be used in production environment.

Originally written across ruby 1.9.2-p290 data structures.
Does work on other 1.9.2s, 1.9.3, 2.0.0-preview1 and 2.0.0-preview2, but not well-tested yet(output is not proven to be as full etc.).

Currently is under development and output format may differ.

## Installation

Add this line to your application's Gemfile:

    gem 'heap_dump'

And then execute:

    $ bundle

Or install it yourself as:

    $ gem install heap_dump

## Usage

### Dumping heap references
In your code call:

```ruby

HeapDump.dump
```

this will run GC and then create a dump.json with live heap contents.
Json contains one object per line, thus can be easily grepped.

#### Output example/format

Format is not stable yet, but looks like this:

```json

[{"id":"_ROOTS_","stack_and_registers":[70313628419480,70313628419480,70313628419480,"trace",70313627751860],"classes":[70313627319820,70313628530860]}
,{"id":70365602702620,"bt":"T_ARRAY","val":[">=",70365602705060]}
,{"id":70365602847060,"bt":"T_ARRAY","val":[]}
,{"id":70365602702660,"bt":"T_DATA","type_name":"iseq","size":564,"name":"activate_spec","filename":"/Users/vasfed/.rvm/rubies/ruby-1.9.2-p290/lib/ruby/site_ruby/1.9.1/rubygems.rb","line":485,"type":"method","refs_array_id":70365602847060,"coverage":null,"klass":70365602821240,"cref_stack":70365602848300,"defined_method_id":12672}
,{"id":70365602702680,"bt":"T_STRING","val":"activate_spec"}
,{"id":70365602821260,"bt":"T_HASH","val":{"EXEEXT":"","RUBY_SO_NAME":"ruby.1.9.1","arch":"x86_64-darwin11.2.0","bindir":70365603049640,"libdir":70365603050600,"ruby_install_name":"ruby","ruby_version":"1.9.1","rubylibprefix":70365603112080,"sitedir":70365603112440,"sitelibdir":70365603048920,"datadir":70365603049880,"vendordir":70365603112500,"vendorlibdir":70365603048800}}
,{"id":70365602712560,"bt":"T_CLASS","name":"URI::HTTPS","methods":{},"ivs":{"__classpath__":"URI::HTTPS","DEFAULT_PORT":443},"super":70365602771440}
,{"id":70365602771400,"bt":"T_CLASS","name":"Class","methods":{"build":70365602782860},"ivs":{"__attached__":70365602771440},"super":70365611597900}
,{"id":70365602717060,"bt":"T_DATA","type_name":"proc","size":72,"is_lambda":0,"blockprocval":null,"envval":70365602712440,"iseq":{"id":70365600821896,"name":"block in <class:FileList>","filename":"/Users/vasfed/.rvm/gems/ruby-1.9.2-p290/gems/rake-0.9.2.2/lib/rake/file_list.rb","line":743,"type":"block","refs_array_id":70365611724600,"coverage":null,"klass":null,"cref_stack":70365611799480,"defined_method_id":0}}
,{"id":70365603045160,"bt":"T_DATA","type_name":"VM/env","size":128,"refs":[]}
,{"id":70365613258980,"bt":"T_ICLASS","name":"Object","methods":{"==":"(CFUNC)",">":"(CFUNC)",">=":"(CFUNC)","<":"(CFUNC)","<=":"(CFUNC)","between?":"(CFUNC)"},"ivs":{"__classid__":"Comparable"},"super":70365613259120}
,{"id":70151795145340,"bt":"T_DATA","type_name":"proc","size":72,"is_lambda":0,"blockprocval":null,"envval":70151795224840,"iseq":{"id":70151828020360,"name":"block in subscribe","filename":"/Users/vasfed/acceptor.rb","line":91,"type":"block","refs_array_id":70151796420080,"coverage":null,"klass":null,"cref_stack":70151796421080,"defined_method_id":0}}
,{"id":70151795224840,"bt":"T_DATA","type_name":"VM/env","size":120,"refs":[70151806738200,70151796176180,"string1",null,0,70151795224840,null]}
]
```
etc.

bt field is ruby builtin type name.

Where available - val/refs/ivs/etc. field present with hash/array of references.
Long numbers usually are object ids.

### Counting objects in heap

Also heap_dump now includes an object counter, like `ObjectSpace.count_objects`, but capable of counting objects from your namespace

```ruby

HeapDump.count_objects [YourNameSpace, "SomeClass"] # => json string
```

which results in something like:

```json

{
    "total_slots": 56419,
    "free_slots": 12384,
    "basic_types": {
        "T_OBJECT": 1945,
        "T_CLASS": 931,
        "T_MODULE": 68,
        "T_FLOAT": 24,
        "T_STRING": 26557,
        "T_REGEXP": 346,
        "T_ARRAY": 6556,
        "T_HASH": 159,
        "T_STRUCT": 45,
        "T_BIGNUM": 2,
        "T_FILE": 6,
        "T_DATA": 3516,
        "T_MATCH": 15,
        "T_COMPLEX": 1,
        "T_RATIONAL": 10,
        "T_NODE": 3754,
        "T_ICLASS": 100
    },
    "user_types": {
        "YourNameSpace::B": 2,
        "YourNameSpace::A": 3,
        "SomeClass": 1
    }
}
```


### Injecting into live process via GDB

For long-running applications common and recommended usecase is to have some kind of custom action or signal handler in app itself that invokes dump.

But also dump can be invoked from gdb.

run gdb

```bash

gdb `which ruby` $YOUR_PID
```

And then run commands like:

```
call rb_require("/Users/vasfed/work/heap_dump/lib/heap_dump.bundle")
call heapdump_dump("mydump.json")
```

or `call heapdump_dump(0)`, filename defaults to dump.json.

Note that yajl-ruby gem (and heap_dump itself) should be available to process this being injected into.
Also on rare ocassions process(for example if gdb attached while a signal/gc) may crash after and even during dumping, so safer way is to embed it in advance, there's no performance overhead.

Object count from gdb: `call (void)heapdump_count_objects_print("Object", "")` (null or empty string terminated list of namespace/class names)
or `call (char*)heapdump_count_objects_return("Object", "")`, note that you then should free memory - `call (void)free($$)`

### Importing dump in MongoDB

Dump can be imported in mongo for some map-reduce, easy script access etc.

```bash

cat dump.json | sed 's/^[,\[]//;s/\]$//;s/^{"id"/{"_id"/' | mongoimport -d database_name -c collection_name --drop --type json
```

Note that even small dumps usually contain a few hundred thousands objects, so do not forget to add some indexes.


## What may leak

### Brief Ruby GC decription
(brief) Ruby has mark-and-sweep GC, this means that it does not leak in traditional way when you lose some pointer and do not free memory.
Ruby leaks references. But also it is different from reference counting GC, like one in python.

For example, 3 objects:

```
A -> B -> C
```
let's assume that A is a global variable or is referenced in stack. In this chain C does not get freed bacause it has a reference path from global/stack.

If reference to B gets deleted (for example A.b = nil):

```
A  B -> C
```
C still is referenced by something, but both B and C will be freed, as there's no path.

### Examples of references

Obvious references: from variables, instance variables (including class), arrays, hashes, etc.

Less obvious: method closures. These are stored in T_DATAs with 'VM/env' type.
Latest version of heap_dump allows to trace such references: search for a env, by it's id you can find it's owner iseq, which usually has file and line number where block/lambda/proc was created.

## Hints on finding leaks

Usually it's a good practice first to detect leaking type by running several `HeapDump.count_objects` while making some load on your app and comparing results. Note objects that may be reasons for others to linger in memory according to your architecture (for example - something like "session"/"incoming connection"/"delayed job" etc.).

Then make a full dump and inspect references to those objects.

Note that heapdump operates only on process it has been called on, so if you have multiple workers (Unicorn/Rainbows/Passenger spawned processes etc.) - you may run into a situation when request for dump is not routed to process you're interested in.

Also it may be a good idea to run dump in forked process or/and on signal:

```ruby

  Signal.trap('USR2') do
    old_pid = Process.pid
    fork {
      puts "Dumping worker #{old_pid}"
      require 'heap_dump'
      HeapDump.dump "dump_#{old_pid}.json"
      exit
    }
  end
```

## Contributing

1. Fork it
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -am 'Added some feature'`)
4. Push to the branch (`git push origin my-new-feature`)
5. Create new Pull Request
