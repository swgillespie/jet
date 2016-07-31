add_executable(jet
    main.cpp
    sexp.cpp
    util.cpp
    reader.cpp 
    gc.cpp 
    interner.cpp
    contract.cpp activation.cpp activation.h meaning.cpp meaning.h analysis.cpp analysis.h builtins.cpp builtins.h)

install (TARGETS jet DESTINATION bin)