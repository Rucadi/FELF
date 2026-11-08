# Symlinks that select a program of a multi-program FELF.
file(MAKE_DIRECTORY "${WORK}/elsewhere")
foreach(name nofile loc unknown)
    file(REMOVE "${WORK}/multi/bin/${name}")
    file(CREATE_LINK "multi" "${WORK}/multi/bin/${name}" SYMBOLIC)
endforeach()
file(REMOVE "${WORK}/elsewhere/nofile")
file(CREATE_LINK "${WORK}/multi/bin/multi" "${WORK}/elsewhere/nofile" SYMBOLIC)
