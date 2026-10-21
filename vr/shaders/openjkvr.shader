// OpenJK VR: shaders the VR game draws with (the game folder's base/shaders)

// holsters (cg_vrholster.c): the faint rings that mark where they are - the
// colour and see-through-ness each corner is drawn with
gfx/vr/holster_ring
{
	cull	none
	nopicmip
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}
