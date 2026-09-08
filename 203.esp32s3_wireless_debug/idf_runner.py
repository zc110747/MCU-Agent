# Git Bash wrapper: strip MSYSTEM then run IDF's idf.py as __main__
import os, sys, runpy
os.environ.pop('MSYSTEM', None)
tools_dir = os.path.join(os.environ['IDF_PATH'], 'tools')
sys.path.insert(0, tools_dir)
sys.argv = ['idf.py'] + sys.argv[1:]
runpy.run_path(os.path.join(tools_dir, 'idf.py'), run_name='__main__')
