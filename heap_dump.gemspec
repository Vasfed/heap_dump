# -*- encoding: utf-8 -*-
require File.expand_path('../lib/heap_dump/version', __FILE__)

Gem::Specification.new do |gem|
  gem.authors       = ["Vasily Fedoseyev"]
  gem.email         = ["vasilyfedoseyev@gmail.com"]
  gem.description   = %q{Ruby 1.9 heap contents dumper}
  gem.summary       = %q{Allows to dump heap to track reference leaks, including leaks in proc contexts and fibers}
  gem.homepage      = "https://github.com/Vasfed/heap_dump"

  gem.executables   = `git ls-files -- bin/*`.split("\n").map{ |f| File.basename(f) }
  gem.files         = `git ls-files`.split("\n")
  gem.test_files    = `git ls-files -- {test,spec,features}/*`.split("\n")
  gem.name          = "heap_dump"
  gem.require_paths = ["lib"]
  gem.version       = HeapDump::VERSION

  gem.required_ruby_version = '>=1.9.2'
  #gem.platform = Gem::Platform::CURRENT # other than osx - maybe later

  gem.extensions = "ext/heap_dump/extconf.rb"

  gem.add_dependency "debugger-ruby_core_source"
  gem.add_dependency 'yajl-ruby', '~>1.1'
  gem.add_development_dependency "rake-compiler"
end
