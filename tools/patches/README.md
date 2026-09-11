XenonRecomp is pulled in as a submodule, pinned at upstream commit ddd128bcca99fe8bfbb99bea583c972351fa6ace.
The patch beside this file is what this project changes in it: the integer
divide guard (a PowerPC divide by zero does not trap, the host does) and the
context header. Apply it after checking the submodule out:

    git -C tools/XenonRecomp apply ../patches/XenonRecomp.patch