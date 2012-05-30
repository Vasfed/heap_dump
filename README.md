# HeapDump

Low-level ruby heap memory dump - including data and code references.
Written across ruby 1.9.2-p290 data structures. Other rubies support may come later (or may not).

Currently is under development and output format may differ.

## Installation

Add this line to your application's Gemfile:

    gem 'heap_dump'

And then execute:

    $ bundle

Or install it yourself as:

    $ gem install heap_dump

## Usage

In your code call:

```ruby

HeapDump.dump
```

this will run GC and then create a dump.json with live heap contents.
Json contains one object per line, thus can be easily grepped.

### Injecting into live process via GDB

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

Note that ruby-internal and yajl-ruby gems should be available to process this being injected into.

### Importing dump in MongoDB

Dump can be imported in mongo for some map-reduce, easy script access etc.

```bash

cat dump.json | sed 's/^[,\[]//;s/\]$//;s/^{"id"/{"_id"/' | mongoimport -d database_name -c collection_name --drop --type json
```

Note that even small dumps usually contain a few hundred thousands objects, so do not forget to add some indexes.


### Output example/format

Format is not stable yet, but looks like this:

```json

[{"id":"_ROOTS_","stack_and_registers":[70313628419480,70313628419480,70313628419480,"trace",70313627751860],"classes":[70313627319820,70313628530860]}
,{"id":70313614304580,"bt":"T_DATA","type_name":"iseq","size":564,"name":"activate_spec","filename":"/Users/vasfed/.rvm/rubies/ruby-1.9.2-p290/lib/ruby/site_ruby/1.9.1/rubygems.rb","line":485,"type":"method","refs_array_id":70313614448980,"coverage":null,"klass":70313614423160,"cref_stack":70313614450220,"defined_method_id":12672}
,{"id":70365602702620,"bt":"T_ARRAY","val":[">=",70365602705060]}
,{"id":70365602702660,"bt":"T_DATA","type_name":"iseq","size":564,"name":"activate_spec","filename":"/Users/vasfed/.rvm/rubies/ruby-1.9.2-p290/lib/ruby/site_ruby/1.9.1/rubygems.rb","line":485,"type":"method","refs_array_id":70365602847060,"coverage":null,"klass":70365602821240,"cref_stack":70365602848300,"defined_method_id":12672}
,{"id":70365602702680,"bt":"T_STRING","val":"activate_spec"}
,{"id":70365602821260,"bt":"T_HASH","val":{"EXEEXT":"","RUBY_SO_NAME":"ruby.1.9.1","arch":"x86_64-darwin11.2.0","bindir":70365603049640,"libdir":70365603050600,"ruby_install_name":"ruby","ruby_version":"1.9.1","rubylibprefix":70365603112080,"sitedir":70365603112440,"sitelibdir":70365603048920,"datadir":70365603049880,"vendordir":70365603112500,"vendorlibdir":70365603048800}}
,{"id":70365602712560,"bt":"T_CLASS","name":"URI::HTTPS","methods":{},"ivs":{"__classpath__":"URI::HTTPS","DEFAULT_PORT":443},"super":70365602771440}
,{"id":70365602771400,"bt":"T_CLASS","name":"Class","methods":{"build":70365602782860},"ivs":{"__attached__":70365602771440},"super":70365611597900}
,{"id":70365602717060,"bt":"T_DATA","type_name":"proc","size":72,"is_lambda":0,"blockprocval":null,"envval":70365602712440,"iseq":{"id":70365600821896,"name":"block in <class:FileList>","filename":"/Users/vasfed/.rvm/gems/ruby-1.9.2-p290/gems/rake-0.9.2.2/lib/rake/file_list.rb","line":743,"type":"block","refs_array_id":70365611724600,"coverage":null,"klass":null,"cref_stack":70365611799480,"defined_method_id":0}}
,{"id":70365603045160,"bt":"T_DATA","type_name":"VM/env","size":128,"refs":[]}
,{"id":70365613258980,"bt":"T_ICLASS","name":"Object","methods":{"==":"(CFUNC)",">":"(CFUNC)",">=":"(CFUNC)","<":"(CFUNC)","<=":"(CFUNC)","between?":"(CFUNC)"},"ivs":{"__classid__":"Comparable"},"super":70365613259120}
]
```
etc.

bt field is ruby builtin type name.

Where available - val/refs/ivs/etc. field present with hash/array of references.

## Contributing

1. Fork it
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -am 'Added some feature'`)
4. Push to the branch (`git push origin my-new-feature`)
5. Create new Pull Request
