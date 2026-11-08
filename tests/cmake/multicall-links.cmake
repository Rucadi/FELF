# Symlinks that select an alias of a multicall FELF.
foreach(name one two)
    file(REMOVE "${WORK}/multicall/bin/${name}")
    file(CREATE_LINK "box" "${WORK}/multicall/bin/${name}" SYMBOLIC)
endforeach()
