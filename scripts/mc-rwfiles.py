#!/usr/bin/env python3
# Multi physical client r/w file benchmarking script

import subprocess as sp
from sys import argv, exit, stderr
from getopt import getopt, GetoptError
from random import random
from math import fsum

# Order the targets to distribute works evenly
targets = ['0', '4', '1', '5', '2', '6', '3', '7']

# Options
nclients = 1                # Number of clients (-c, --nclients)
dirpath = '/vfs0/files-4K'  # Path to data files
read = True                 # False if we want to write (-r, --read OR -w, --write)
nfiles = 1000               # Number of total files to operate (-n, --nfiles)
overlap = 0                 # Overlap rate (-o, --overlap)
overlap_style = 'random'    # Overlap distribution style (-s, --overlap-style)
                            # Choices: 'random', 'rear', 'front'
use_tc = True               # Use vNFS vectorized operations? (--tc OR --notc)
verbose = False             # More output messages? (-v, --verbose)

commander = './commander.sh'
prog='fsl-tc-client/tc_client/release/tc/tc_rw_files2'

# Parse cmd arguments
try:
    optlist, args = getopt(argv[1:], 'c:rwn:o:s:v', [
        'nclients=', 'read', 'write', 'nfiles=', 'overlap=', 'overlap-style=',
        'tc', 'notc', 'verbose'])
except GetoptError as e:
    print(str(e))
    exit(1)

for key, value in optlist:
    if key in ('-v', '--verbose'):
        verbose = True
    elif key in ('-c', '--nclients'):
        nclients = int(value)
    elif key in ('-r', '--read'):
        read = True
    elif key in ('-w', '--write'):
        read = False
    elif key in ('-n', '--nfiles'):
        nfiles = int(value)
    elif key in ('-o', '--overlap'):
        overlap = int(value)
    elif key in ('-s', '--overlap-style'):
        overlap_style = value
    elif key == '--tc':
        use_tc = True
    elif key == '--notc':
        use_tc = False

if len(args) > 0:
    dirpath = args[0]

# If verbose option is ON, print options
if verbose:
    print('Number of clients: %d' % nclients)
    print('Path to files to %s: %s' % (('read' if read else 'write'), dirpath))
    print('Number of files to operate: %d' % nfiles)
    print('Overlap rate: %d%%, in the form of %s' % (overlap, overlap_style))
    print('Use vectorization? %s' % str(use_tc))


def arrange_task(indiv_start, indiv_end, common_start, common_end, style):
    tasklist = []
    if style == 'front':
        for i in range(common_start, common_end):
            tasklist.append(i)
        for i in range(indiv_start, indiv_end):
            tasklist.append(i)
    elif style == 'rear':
        for i in range(indiv_start, indiv_end):
            tasklist.append(i)
        for i in range(common_start, common_end):
            tasklist.append(i)
    elif style == 'random':
        p1 = indiv_start
        p2 = common_start
        total = (indiv_end - indiv_start) + (common_end - common_start)
        common_prob = 1.0 * (common_end - common_start) / total

        while p1 < indiv_end and p2 < common_end:
            rn = random()
            if rn <= common_prob:
                tasklist.append(p2)
                p2 += 1
            else:
                tasklist.append(p1)
                p1 += 1

        if p1 < indiv_end:
            for i in range(p1, indiv_end):
                tasklist.append(i)
        if p2 < common_end:
            for i in range(p2, common_end):
                tasklist.append(i)
    return tasklist


commander_arg_list = [commander, '-p', '-q', '-t', ' '.join(targets[:nclients])]

# Compose command args
filenum = 0
files_per_client = nfiles // nclients
commons = files_per_client * overlap // 100
independants = files_per_client - commons
for i in range(nclients):
    commander_arg_list.append('-c')
    tasks = arrange_task(filenum, filenum + files_per_client, nfiles - commons, nfiles, overlap_style)
    taskstr = ','.join([str(i) for i in tasks])
    cmd = ['sudo', prog]
    if not use_tc:
        cmd.append('--notc')
    if not read:
        cmd.append('--noread')
    if verbose:
        cmd.append('--verbose')
    cmd.append('--tasks')
    cmd.append(taskstr)
    cmd.append(dirpath)
    # commander_arg_list.append('"%s"' % ' '.join(cmd))
    commander_arg_list.append(' '.join(cmd))

# Execute commander
p = sp.Popen(commander_arg_list, stderr=sp.PIPE, stdout=sp.PIPE)
p.wait()

output = p.stdout.read().decode('utf-8')
errmsg = p.stderr.read().decode('utf-8')

if verbose:
    print('stdout message:')
    print('>>>>>>>>>>>>>>>>>>>>>>>')
    print(output)
    print('<<<<<<<<<<<<<<<<<<<<<<<')
    print('stderr message:')
    print('>>>>>>>>>>>>>>>>>>>>>>>')
    print(errmsg)
    print('<<<<<<<<<<<<<<<<<<<<<<<')

# Aggregate result
print('#total_throughput(MB/s),max_time(sec),min_time(sec),throughput_per_client(MB/s)...,time_per_client(sec)', file=stderr)

thruputs = []
times = []
for line in output.splitlines():
    a, b = line.split(', ')
    time = float(a.split(' ')[0])
    tp = float(b.split(' ')[0])
    thruputs.append(tp)
    times.append(time)

print('%.6f,%.6f,%.6f,%s,%s' % (
    fsum(thruputs), max(times), min(times), ','.join(['%.6f' % tp for tp in thruputs]),
    ','.join(['%.6f' % t for t in times])
))

