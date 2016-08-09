add_executable(jet
    main.cpp
    sexp.cpp
    util.cpp
    reader.cpp 
    gc.cpp 
    interner.cpp
    contract.cpp
    activation.cpp
    meaning.cpp 
    analysis.cpp 
    builtins.cpp
    options.cpp)

install (TARGETS jet DESTINATION bin)