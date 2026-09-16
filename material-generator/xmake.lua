target("material-generator")
    set_kind("binary")
    set_languages("cxx23")

    add_files("src/*.cpp")
    add_packages(
        "fastgltf",
        "stb"
    )
    add_rules("defaults_rule")
