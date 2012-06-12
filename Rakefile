#!/usr/bin/env rake
require "bundler/gem_tasks"


require 'rake/extensiontask'
Rake::ExtensionTask.new('heap_dump')

desc "Simple dump test,just to check if extension compiles and does not segfault on simple dump"
task :test => :compile do
  require "bundler/setup"
  require 'heap_dump'

  def some_meth
    fiber_var = :some_fiber_var2
    e = [1,2,3].each # makes enumerator, which references fiber
    Fiber.yield
    aaa
  end

  class Fiber
    def [](key)
      local_fiber_variables[key]
    end
    def []=(key,value)
      local_fiber_variables[key] = value
    end
    private
    def local_fiber_variables
      @local_fiber_variables ||= {}
    end
  end
  require 'fiber'
  Fiber.new{
      fiber_var = :some_fiber_var
      Fiber.current[:some_fiber_ivar] = :ivar_cool_value
      some_meth
      Fiber.yield e
      fiber_var = :some_fiber_var3
    }.resume
  puts "Dumping..."
  HeapDump.dump
end

task :default => :test