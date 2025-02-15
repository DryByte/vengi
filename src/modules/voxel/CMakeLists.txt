set(LIB voxel)
set(SRCS
	ChunkMesh.h
	CubicSurfaceExtractor.h CubicSurfaceExtractor.cpp
	MarchingCubesSurfaceExtractor.h MarchingCubesSurfaceExtractor.cpp
	MarchingCubesTables.h
	Face.h Face.cpp
	MaterialColor.h MaterialColor.cpp
	Mesh.h Mesh.cpp
	Palette.h Palette.cpp
	PaletteLookup.h
	RawVolume.h RawVolume.cpp
	RawVolumeWrapper.h
	RawVolumeMoveWrapper.h
	Region.h Region.cpp
	VoxelVertex.h
	Voxel.h Voxel.cpp
)
engine_add_module(TARGET ${LIB} SRCS ${SRCS} DEPENDENCIES util image commonlua)
set(MARCH native)
#set(MARCH generic)
# http://christian-seiler.de/projekte/fpmath/
if (MSVC)
	# Errors out with RTC1 and Ox are incompatible
	#target_compile_options(${LIB} PRIVATE /Ox)
else()
	check_c_compiler_flag(-mtune=${MARCH} HAVE_FLAG_MTUNE_${MARCH})
	if (HAVE_FLAG_MTUNE_${MARCH})
		target_compile_options(${LIB} PRIVATE -mtune=${MARCH})
	endif()
	target_compile_options(${LIB} PRIVATE -O3)
endif()

set(TEST_SRCS
	tests/AbstractVoxelTest.h
	tests/FaceTest.cpp
	tests/PaletteTest.cpp
	tests/PolyVoxTest.cpp
	tests/RegionTest.cpp
	tests/RawVolumeWrapperTest.cpp
)

set(TEST_FILES
	tests/test-palette-in.png
)

gtest_suite_begin(tests-${LIB} TEMPLATE ${ROOT_DIR}/src/modules/core/tests/main.cpp.in)
gtest_suite_sources(tests-${LIB} ${TEST_SRCS})
gtest_suite_deps(tests-${LIB} ${LIB} test-app)
gtest_suite_files(tests-${LIB} ${FILES} ${TEST_FILES})
gtest_suite_end(tests-${LIB})
