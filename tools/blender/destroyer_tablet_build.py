import os
import runpy
from pathlib import Path

# Rebuild the same design with smaller curves and omitted fine details,
# preserving the major hull and weapon geometry for the tablet.
os.environ['DESTROYER_TABLET'] = '1'
runpy.run_path(str(Path(__file__).with_name('destroyer_build.py')), run_name='__main__')
