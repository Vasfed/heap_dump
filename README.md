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

## Contributing

1. Fork it
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -am 'Added some feature'`)
4. Push to the branch (`git push origin my-new-feature`)
5. Create new Pull Request
