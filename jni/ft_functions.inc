#ifndef ADD_FUNC
#error "ADD_FUNC marco not defined"
#endif

ADD_FUNC(FT_Init_FreeType);
ADD_FUNC(FT_New_Face);
ADD_FUNC(FT_Set_Char_Size);
ADD_FUNC(FT_Load_Char);
ADD_FUNC(FT_Done_Face);
ADD_FUNC(FT_Done_FreeType);
ADD_FUNC(FT_Select_Charmap);

#undef ADD_FUNC


