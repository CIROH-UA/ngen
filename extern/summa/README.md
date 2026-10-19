# SummaSundials Submodule

## About

This directory wraps the *summa* Git submodule repo, which contains a clone of the repo for the OWP SummaSundials module library that implements BMI.  From here, the shared library file for the SummaSundials module can be built for use in NGen.  This is configured with the [CMakeLists.txt](CMakeLists.txt) and other files in this outer directory.

#### Extra Outer Directory

Currently there are two directory layers beneath the top-level *extern/* directory.  This was done so that certain things used by NGen (i.e., a *CMakeLists.txt* file for building shared library files) can be placed alongside, but not within, the submodule.

## Working with the Submodule

Some simple explanations of several command actions are included below.  To better understand what these things are doing, consult the [Git Submodule documentation](https://git-scm.com/book/en/v2/Git-Tools-Submodules). 

### Getting the Latest Changes

There are two steps to getting upstream submodule changes fully 
  1. fetching and locally checking out the changes from the remote
  2. committing the new checkout revision for the submodule

To fetch and check out the latest revision (for the [currently used branch](#viewing-the-current-branch)):

    $ git submodule update --init -- extern/summa/summa

To commit the current submodule checkout revision to the NGen repo:

    $ git add extern/summa/summa
    $ git commit

### Viewing the Commit Hash

Git submodule configurations include the specific commit to be checked out (or an implicit default).  The current commit can be view with `git submodule status`:

    $ git submodule status -- extern/summa/summa/

This will show the **commit**, **submodule local path**, and the git description for the **commit**.  The specific configuration, including the configured branch, is set in the _.gitmodules_ file in the NGen project root.

### Changing the Commit Branch

The latest commit in the configured branch can be brought in as described here.  If it is ever necessary to change to a different branch, the following will do so:

    $ git config -f .gitmodules "submodule.extern/summa/summa.branch" <branchName>

Note that this will be done in the NGen repo configuration, so it can then be committed and push to remotes.  It is also possible to do something similar in just the local clone of a repo, by configuring `.git/config` instead of `.gitmodules`.  See the Git documentation for more on how that works if needed.

# Usage

## Building Libraries

If you plan on using any of the NextGen python modules (e.g. routing with t-route), you likely need to build a python environment for NextGen.  An example environment is included in `summa/utils/test/test_ngen/python_env/environment.yml`. ON a Linux machine, you can run
    $ cd ${NGEN_DIR}/ngen/extern/summa/summa/utils/test/test_ngen/python_env
    $ conda env create -f environment.yml -n ngen  
    $ conda activate ngen
On a Mac, you can run 
    $ cd ${NGEN_DIR}/ngen/extern/summa/summa/utils/test/test_ngen/python_env
    $ conda install -n base -c conda-forge mamba -y
    $ export CONDA_SUBDIR=osx-arm64
    $ mamba env create -f environment.yml -n ngen
    $ unset CONDA_SUBDIR
    $ conda activate ngen

If you want to use Sundials IDA or BE Kinsol, before summa can be built Sundials needs to be installed. 

### Installing SUNDIALS
Download the file `sundials-X.Y.Z.tar.gz` (where X.Y.Z is the latest SUNDIALS version) at https://github.com/LLNL/sundials/releases/latest. Move `sundials-X.Y.Z.tar.gz` into `top_dir`. In other words, `$ ls top_dir` should include `summa sundials-X.Y.Z.tar.gz. Download this to the folder your preferred ${SUN_DIR}.

Extract the corresponding compressed file and rename
    $ cd ${SUN_DIR}
    $ tar -xzf sundials-X.Y.Z.tar.gz && mv sundials-X.Y.Z sundials-software
    $ rm sundials-X.Y.Z.tar.gz

Create new empty directories to prep for SUNDIALS installation, within ${SUN_DIR}:
    $ mkdir sundials
    $ cd sundials
    $ mkdir builddir instdir

Copy CMake build script from SUMMA files to properly configure SUNDIALS from your chosen ${NGEN_DIR}
    $ cd builddir
    $ cp ${NGEN_DIR}/ngen/external/summa/summa/build/cmake_external/build_cmakeSundials.bash .

Build SUNDIALS configured for SUMMA, within `builddir`: 
    $ ./build_cmakeSundials.bash
    $ make
    $ make install

We suggest you periodically update to the latest version. It is also possible to install using Git: 
    $ git clone https://github.com/LLNL/sundials.git sundials-software
    $ cd sundials-software
    $ git fetch --all --tags --prune
    $ git checkout tags/vX.Y.Z     
    
Note if you need to recompile after a system upgrade, delete the contents of sundials/instdir and sundials/buildir EXCEPT sundials/buildir/build_cmakeSundials.bash before building and installing.

### Building and installing SUMMA within Next Gen
We suggest using a version of the example build script for SUMMA and NextGen included in `summa/build/cmake/build_ngen.[mac or cluster].bash` (be sure to look a the `mac` version if you are running on a Mac, a Linux machine can use a variant of the cluster script). 
Note (similar to above) that when there is an existing directory, it may sometimes be necessary to clear it and regenerate, especially if any changes were made to the CMakeLists.txt file.

Sundials is turned off in these example scripts with `-DUSE_SUNDIALS=OFF`, but if you installed Sundials as above, you can turn this ON.

Copy this script (perhaps modified) to the directory above your main ngen directory, and run from inside the main ngen directory.
    $ cp ${NGEN_DIR}/ngen/extern/summa/summa/build/cmake/build_ngen.[mac or cluster].bash ${NGEN_DIR}/build_ngen.bash
    $ cd ${NGEN_DIR}/ngen
    $ ./build_ngen.bash

The example build scripts activate the conda environment named by `NGEN_CONDA_ENV` (default
`ngen`) and pass that interpreter to CMake.  Next Gen does not support `numpy>=2.0`, so that
environment must have `numpy<2` (the `environment.yml` above pins this).

### Building t-route (for routing)

Routing is provided by the *t-route* Python package in `${NGEN_DIR}/ngen/extern/t-route`, which
has Cython/Fortran extensions that must be compiled and installed into the *same* Python
environment the ngen build used.  Use the bundled script (run it with that environment active):

    $ conda activate ngen                                 # the numpy<2 / Cython env from above
    $ cd ${NGEN_DIR}/ngen/extern/t-route
    $ F90=<gfortran> CC=<gcc> ./compiler.sh               # Linux
    $ F90=/opt/local/bin/gfortran CC=/opt/local/bin/gcc ./compiler_mac.sh   # Mac

It builds the kernel objects under `src/kernel/{muskingum,diffusive,reservoir}` and then
`pip install`s the `troute-*` packages in dependency order (network, routing, config, nwm, bmi).
Pass `no-e` as the first argument to install them non-editable.

Do not run `pip install -e extern/t-route/src/troute-*` by hand; it skips the kernel builds,
installs in the wrong order, and only makes the first path editable.  Two common failures:
  - `Could not identify fortran compiler!` -- `F90` (or `FC`) is unset, so the build falls back
    to `which fc` and finds the shell `fc` builtin.  Export `F90`/`FC` to your gfortran.
  - `No module named 'Cython'` -- the wrong environment is active (e.g. conda `base`).
    `compiler.sh` uses `--no-build-isolation`, so Cython, `numpy<2` and wheel must already be
    installed there.  Check with `python -c "import sys; print(sys.prefix)"`.

To run the gauge 01073000 test basin, from the main ngen directory run

    $ ./extern/summa/summa/utils/test/test_ngen/example_run.sh

This is the recommended path: the script activates the `ngen` conda environment (override with
`NGEN_CONDA_ENV`), pins `VIRTUAL_ENV` to it, checks that `nwm_routing` imports, then runs

    $ ./cmake_build/ngen ./test/data/routing/gauge_01073000.gpkg '' ./test/data/routing/gauge_01073000.gpkg '' ./extern/summa/summa/utils/test/test_ngen/gauge_01073000/settings/example_realization_config_w_summa_bmi.json
    $ python -m nwm_routing -V4 -f ./test/data/routing/ngen_routing_config_unit_test.yaml

The first command runs SUMMA through its BMI; the second routes the resulting nexus output with
t-route.  To run ngen only (no routing), run just the first command with the
`example_realization_config_w_summa_bmi.json` realization.  `domain_provo/` has an equivalent
`provo_run.sh`.

If you run the `ngen` command by hand rather than through the script, first
`conda activate <your ngen env>` **and** `export VIRTUAL_ENV="$CONDA_PREFIX"` -- ngen globs
`**/site-packages/` under `$VIRTUAL_ENV` for its embedded interpreter, so a `VIRTUAL_ENV` left
pointing at a conda root (VS Code's Python extension does this) pulls every environment's
packages onto `sys.path` and segfaults on an incompatible NumPy.

