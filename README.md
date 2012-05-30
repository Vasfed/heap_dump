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

Or call from gdb function (null filename defaults to 'dump.json')

```c

void heapdump_dump(const char* filename);
```

this will run GC and then create a dump.json with live heap contents.
Json contains one object per line, thus can be easily grepped.

### Importing dump in MongoDB

Dump can be imported in mongo for some map-reduce, easy script access etc.

```bash

cat dump.json | sed 's/^[,\[]//;s/\]$//;s/^{"id"/{"_id"/' | mongoimport -d database_name -c collection_name --drop --type json
```

Note that even small dumps usually contain a few hundred thousands objects, so do not forget to add some indexes.


## Contributing

1. Fork it
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -am 'Added some feature'`)
4. Push to the branch (`git push origin my-new-feature`)
5. Create new Pull Request
