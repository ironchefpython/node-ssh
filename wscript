import Options
import os, re
from os.path import exists 

srcdir = '.'
blddir = 'build'

def set_options(opt):
    opt.tool_options('compiler_cxx')

def configure(conf):
    conf.check_tool('compiler_cxx')
    conf.check_tool('node_addon')
    
    libs = re.split(r'\s+', os.popen(
        'pkg-config --libs libssh'
    ).readline().strip())
    conf.env.append_value('LINKFLAGS', libs)

def build(bld):
    ssh = bld.new_task_gen('cxx', 'shlib', 'node_addon')
    ssh.target = 'ssh'
    ssh.source = [
        'src/init.cc', 'src/sshBase.cc', 'src/sftp.cc', 'src/tunnel.cc'
    ]
    ssh.cxxflags = [ '-D_FILE_OFFSET_BITS=64', '-D_LARGEFILE_SOURCE' ]
    
    ssh.staticlib = 'ssh'
