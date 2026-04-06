#ifdef FIELD_PREP
#undef FIELD_PREP
#endif
#ifdef FIELD_GET
#undef FIELD_GET
#endif
#define FIELD_PREP(mask, value) (((value) * (__builtin_ffsll(mask))) & (mask))
#define FIELD_GET(mask, value)  (((value) & (mask)) / (__builtin_ffsll(mask)))
