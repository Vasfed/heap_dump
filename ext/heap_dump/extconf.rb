require 'mkmf'

def find_spec name,*requirements
  return Gem::Specification.find_by_name(name, *requirements) if Gem::Specification.respond_to? :find_by_name

  requirements = Gem::Requirement.default if requirements.empty?

  gem = Gem::Dependency.new(name, *requirements)
  matches = Gem.source_index.find_name(gem.name, gem.requirement)
  raise "No matching #{name} gem!" unless matches.any?
  matches.find { |spec|
    Gem.loaded_specs[gem.name] == spec
    } or matches.last
end

def find_gem_dir(name, *req)
  gem = find_spec(name, *req)
  return gem.gem_dir if gem.respond_to? :gem_dir
  gem.full_gem_path
end

dir = find_gem_dir('ruby-internal', '~>0.8.5') #FIXME: DRY (see gemfile)
find_header('version.h', File.join(dir, 'ext', 'internal', 'yarv-headers'))
find_header('yarv-headers/node.h', File.join(dir, 'ext', 'internal'))
find_header('internal/method/internal_method.h', File.join(dir, 'ext'))

yajl = find_gem_dir('yajl-ruby', '~>1.1')
find_header('api/yajl_gen.h', File.join(yajl, 'ext', 'yajl'))

create_makefile('heap_dump')