#
# Hello world Waf script
#
from __future__ import print_function

rtems_version = "7"

try:
    import rtems_waf.rtems as rtems
except:
    print('error: no rtems_waf git submodule')
    import sys
    sys.exit(1)

def init(ctx):
    rtems.init(ctx, version = rtems_version, long_commands = True)

def bsp_configure(conf, arch_bsp):
    # Add BSP specific configuration checks
    pass

def options(opt):
    rtems.options(opt)

def configure(conf):
    rtems.configure(conf, bsp_configure = bsp_configure)

def build(bld):
    rtems.build(bld)
    
    
    sources = (
        bld.path.ant_glob('src/*.c') +
        bld.path.ant_glob('test/*.c')
    )
    bld(features = 'c cprogram ',
        target = 'hello.exe',
        cflags = '-g -O2',
        includes = ['src'],
        source = sources,
        use='bsd' , 
        lib = ['bsd', 'm', 'z', 'rtemstest'],  
    )