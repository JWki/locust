-- workspace for runtime libraries/executables
workspace("locust")
    configurations {"Debug", "Release"}
    platforms {"x64_dev", "x64_deploy"}
    location("workspace")
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
            defines {"GT_DEVELOPMENT"}
        filter "configurations:Debug"
            defines {"GT_DEBUG"}
        filter "configurations:Release"
            defines {"GT_NODEBUG"}
    filter {}

    -- create some helper functions

    function make_exe(name, main_dir)
        project(name)
        kind("ConsoleApp")
        language("C++")
        targetdir(main_dir .. "/bin/%{cfg.buildcfg}")
        location(main_dir .. "/workspace/" .. name)
        debugdir(main_dir .. "/bin/%{cfg.buildcfg}")
        files { "**.c", "**.cpp", "**.h"}
        filter {"system:not windows"}
            excludes { "**win32/**" }

        filter {"system:not linux"}
            excludes { "**linux/**" }
        filter "configurations:Debug"
            defines {"GT_DEBUG"}
            symbols "On"
        filter "configurations:Release"
            defines {"GT_NODEBUG"}
            optimize "On"
        filter {}
    end
            
    
    function make_dll(name, main_dir)
        project(name)
        kind("SharedLib")
        defines { "GT_SHARED_LIB" }
        language("C++")
        targetdir(main_dir .. "/bin/%{cfg.buildcfg}")
        location(main_dir .. "/workspace/" .. name)
        debugdir(main_dir .. "/bin/%{cfg.buildcfg}")
        files { "**.c", "**.cpp", "**.h"}
        filter {"system:not windows"}
            excludes { "**win32/**" }
        filter {"system:not linux"}
            excludes { "**linux/**" }
        filter "configurations:Debug"
            defines {"GT_DEBUG"}
            symbols "On"
        filter "configurations:Release"
            defines {"GT_NODEBUG"}
            optimize "On"
        filter{"system:windows"}
        local rand = "$([System.DateTime]::Now.ToString(\"HH_mm_ss_fff\"))"
        linkoptions {"/PDB:\"" .. name .. "_" .. rand .. ".pdb\""}
        filter {}
    end

    function make_lib(name, main_dir)
        project(name)
        kind("StaticLib")
        language("C++")
        targetdir(main_dir .. "/bin/%{cfg.buildcfg}")
        location(main_dir .. "/workspace/" .. name)
        debugdir(main_dir .. "/bin/%{cfg.buildcfg}")
        files { "**.c", "**.cpp", "**.h"}
        filter {"system:not windows"}
            excludes { "**win32/**" }
        filter {"system:not linux"}
            excludes { "**linux/**" }
        filter "configurations:Debug"
            defines {"GT_DEBUG"}
            symbols "On"
        filter "configurations:Release"
            defines {"GT_NODEBUG"}
            optimize "On"
        filter {}
    end


    -- run through src/ folder and check for scripts
    sourceDirectories = os.matchdirs("src/**")
    for k, dir in pairs(sourceDirectories) do
        print("Source directory: " .. dir)
        name = string.gsub(dir, "src/", "")
        scriptFile = dir .. "/premake5.lua"
        --print("Trying to execute " .. scriptFile)
        main_dir = os.getcwd()
        group(name)
        success = dofileopt(scriptFile)
        if(success) then print("Added project " .. name) end
    end