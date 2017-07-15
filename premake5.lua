-- workspace for runtime libraries/executables
workspace("locust")
    configurations {"Debug", "Release"}
    platforms {"x64_dev", "x64_deploy"}
    location("workspaces/locust")
    -- compiler/linker flags
        flags {"FatalWarnings", "C++14", "NoImportLib", "MultiProcessorCompile", "NoImplicitLink", "NoMinimalRebuild"}
    -- disable rtti and exceptions
        exceptionhandling("Off")
        rtti("Off")
    -- library/include paths
        libdirs{"dependencies/lib"}
        includedirs{"dependencies/include", "src"}
    -- platform settings
        filter {"platforms:x64*"}
            architecture "x64"
        filter {"platforms:x64_dev"}
            defines {"LC_DEVELOPMENT"}
        filter "configurations:Debug"
            defines {"LC_DEBUG"}
        filter "configurations:Release"
            defines {"LC_NODEBUG"}
    filter {}

    -- 
    group("shared")

    -- core libraries (containers, allocators, etc), statically linked into runtime, editor, etc
        project("locust_core_lib")
            kind("StaticLib")
            language("C++")
            targetdir("bin/%{cfg.buildcfg}")
            location("workspaces/locust/locust_core_lib")
            debugdir("bin/%{cfg.buildcfg}")
            files {"src/core_lib/**.h", "src/core_lib/**.c", "src/core_lib/**.cpp"}
            filter {"system:not windows"}
                excludes {"src/core_lib/win32/**"}
            filter {"system:not linux"}
                excludes {"src/core_lib/linux/**"}
            filter "configurations:Debug"
                symbols "On"
            filter "configurations:Release"
                optimize "On"
            filter{}


    -- 
    group ("runtime")

    -- main executable
    project("locust_runtime")
        kind("ConsoleApp")
        language("C++")
        targetdir("bin/%{cfg.buildcfg}")
        location("workspaces/locust/locust_runtime")
        debugdir("bin/%{cfg.buildcfg}")
        files {"src/runtime/**.h", "src/runtime/**.c", "src/runtime/**.cpp"}
        filter {"system:not windows"}
            excludes {"src/runtime/win32/**"}
        filter {"system:not linux"}
            excludes {"src/runtime/linux/**"}
        filter "configurations:Debug"
            symbols "On"
        filter "configurations:Release"
            optimize "On"
        filter{}

        links("locust_core_lib")



