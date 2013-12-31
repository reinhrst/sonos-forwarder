/**
 * Replaces occurences of one string with the next. Returns a new char* which needs to be free'd later
 * if max_replacements == STR_REPLACE_REPLACE_ALL, then all occurences are replaced
 **/
#define STR_REPLACE_REPLACE_ALL 0
char *str_replace(const char *orig, const char *rep, const char *with, unsigned int max_replacements);
