require 'mkmf'

spec = Gem::Specification.find_by_name('ruby-internal', '~>0.8.5') #FIXME: DRY (see gemfile)
find_header('version.h', File.join(spec.gem_dir, 'ext', 'internal', 'yarv-headers'))
find_header('yarv-headers/node.h', File.join(spec.gem_dir, 'ext', 'internal'))

yajl = Gem::Specification.find_by_name('yajl-ruby', '~>1.1')
find_header('api/yajl_gen.h', File.join(yajl.gem_dir, 'ext', 'yajl'))

create_makefile('heap_dump')