# libobs via node bindings
This library intends to provide bindings to obs-studio's internal library, named libobs accordingly, for the purpose of using it from a node runtime.
Currently, only Windows and MacOS are supported.

# Building

## Prerequisites
You will need to have the following installed:

* Git
* [Node.js](https://nodejs.org/en/)
* [Yarn](https://yarnpkg.com/en/docs/install#windows-stable)
* [CMake](https://cmake.org/)

### Windows
Building on windows requires additional software:

* [Visual Studio 2019 or 2022](https://visualstudio.microsoft.com/)
* [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk) (may be installed by Visual Studio 2022 Installer)

### Mac
Building on mac requires Xcode.

Example (this assumes desktop repo is also installed in same parent folder and attempts to build a test developer app bundle so you can see browser sources, etc):
```
yarn install
git submodule update --init --recursive
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=../desktop/node_modules/obs-studio-node/OSN.app/distribute/obs-studio-node -DCMAKE_OSX_ARCHITECTURES=arm64 -G Xcode
cmake --build . --target install --config RelWithDebInfo
```

### Example Build
We use a flexible cmake script to be as broad and generic as possible in order to prevent the need to constantly manage the cmake script for custom uses, while also providing sane defaults. It follows a pretty standard cmake layout and you may execute it however you want.

Example:
```
yarn install
git submodule update --init --recursive
mkdir build
cd build
cmake .. -G"Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=%CD%/libobs-src/cmake/
cmake --build . --target install --config RelWithDebInfo
```

This will will download any required dependencies, build the module, and then place it in an archive compatible with npm or yarn that you may specify in a given package.json.

### Custom OBS Build
By default, we download a pre-built version of libobs if none is specified. However, this pre-built version may not be what you want to use or maybe you're testing a new obs feature.

You may specify a custom archive of your own. However, some changes need to be made to obs-studio's default configuration before building:

* `ENABLE_SCRIPTING` must be set to `false`
* `ENABLE_UI` must be set to `false`
* `QTDIR` should *not* be specified as it is not used.

If you don't know how to build obs-studio from source, you may find instructions [here](https://github.com/obsproject/obs-studio/wiki/Install-Instructions#windows-build-directions).


#### cppcheck 

Install cppcheck from http://cppcheck.sourceforge.net/ and add cppcheck folder to PATH 
To run check from command line:  
```
cd build 
cmake --build . --target CPPCHECK
```

Also target can be built from Visula Studio. 
Report output format set as compatible and navigation to file:line posiible from build results panel.  

Some warnings suppressed in files `obs-studio-client/cppcheck_suppressions_list.txt` and `obs-studio-server/cppcheck_suppressions_list.txt`.

#### Clang Analyzer 

`Ninja` and `LLVM` have to be installed in system. Warning: depot_tool have broken ninja.  
To make build open `cmd.exe`. 


```
mkdir build_clang
cd build_clang

"c:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\bin\amd64\vcvars64.bat"
 
set CCC_CC=clang-cl
set CCC_CXX=clang-cl
set CC=ccc-analyzer.bat
set CXX=c++-analyzer.bat
#set CCC_ANALYZER_VERBOSE=1

#make ninja project 
cmake  -G "Ninja" -DCLANG_ANALYZE_CONFIG=1 -DCMAKE_INSTALL_PREFIX:PATH=""  -DCMAKE_LINKER=lld-link -DCMAKE_BUILD_TYPE="Debug"   -DCMAKE_SYSTEM_NAME="Generic" -DCMAKE_MAKE_PROGRAM=ninja.exe ..

#try to build and "fix" errors 
ninja.exe 

#clean build to scan 
ninja.exe clean 

scan-build --keep-empty -internal-stats -stats -v -v -v -o check ninja.exe
```
Step with `"fixing"` errors is important as code base and especially third-party code are not ready to be build with clang. And files which failed to compile will not be scanned for errors.

### Tests

The tests for obs studio node are written in Typescript and use Mocha as test framework, with electron-mocha pacakage to make Mocha run in Electron, and Chai as assertion framework.

You need to build obs-studio-node in order to run the tests. You can build it any way you want, just be sure to use `CMAKE_INSTALL_PREFIX` to install obs-studio-node in a folder of your choosing. The tests use this variable to know where the obs-studio-node module is. Since we use our own fork of Electron, you also need to create an environment variable called `ELECTRON_PATH` pointing to where the Electron binary is in the node_modules folder after you run `yarn install`. Below are three different ways to build obs-studio-node:

#### Terminal commands
In obs-studio-node root folder:
1. `yarn install`
2. `git submodule update --init --recursive --force`
3. `mkdir build`
4. `cmake -Bbuild -H. -G"Visual Studio 16 2019" -A x64 -DCMAKE_INSTALL_PREFIX="path_of_your_choosing"`
5. `cmake --build build --target install`

#### Terminal using package.json scripts
In obs-studio-node root folder:
1. `mkdir build`
2. `yarn local:config`
3. `yarn local:build`
4. Optional: To clean build folder to repeat the steps 2 to 3 again do `yarn local:clean`

#### CMake GUI
1. `yarn install`
2. Create a build folder in obs-studio-node root
3. Open CMake GUI
4. Put obs-studio-node project path in `Where is the source code:` box
5. Put path to build folder in `Where to build the binaries:` box
6. Click `Configure`
7. Change CMAKE_INSTALL_PREFIX to a folder path of your choosing
8. Click `Generate`
9. Click `Open Project` to open Visual Studio and build the project there

#### Running tests
Some tests interact with Twitch and we use a user pool service to get users but in case we are not able to fetch a user from it, we use the stream key provided by an environment variable. Create an environment variable called SLOBS_BE_STREAMKEY with the stream key of a Twitch account of your choosing.

* To run all the tests do `yarn run test` 
* To run only run one test do `yarn run test --grep describe_name_value` where `describe_name_value` is the name of the test passed to the describe call in each test file. Examples: `yarn run test --grep nodeobs_api` or `yarn run test -g "Start streaming"`

#### Debugging tests
If you wish to debug a specific test change the wildcard character to the name of your test (for ex, change `*.ts` into `test_nodeobs_api.ts`) in your launch.json:
```
{
    "version": "0.2.0",
    "configurations": [
        {
            "type": "node",
            "request": "launch",
            "name": "Debug Electron-Mocha Tests",
            "program": "${workspaceFolder}/node_modules/electron-mocha/bin/electron-mocha",
            "args": [
                "-r",
                "ts-node/register",
                "${workspaceFolder}/tests/osn-tests/src/*.ts"
            ],
            "cwd": "${workspaceFolder}",
            "env": {
                "ELECTRON_ENABLE_LOGGING": "true"
            },
            "outputCapture": "std", // Enables capturing test output directly
            "internalConsoleOptions": "openOnSessionStart",
            "skipFiles": ["<node_internals>/**"] // Avoid stepping into Electron internals
        }
    ]
}
```