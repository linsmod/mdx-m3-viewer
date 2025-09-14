#include <StormPort.h>
#define KNOWN_AS(STRUCT, TYPE) \
typedef struct STRUCT TYPE; \
typedef struct STRUCT *LP##TYPE; \
typedef struct STRUCT const *LPC##TYPE;

typedef enum {
    T_NUMBER,
    T_STRING,
    T_BOOLEAN,
    T_ARRAY,
    T_OBJECT,
    T_NULL,
    T_UNDEFINED,
    T_FUNCTION,
    T_INTEGER,
    T_FLOAT,
    T_DOUBLE,
} JastType;
KNOWN_AS(jast_object_t_t, OBJECT);
KNOWN_AS(jast_string_t, STRING);
KNOWN_AS(jast_number_t, NUMBER);
KNOWN_AS(jast_integer_t, INTERGER);
KNOWN_AS(jast_float_t, FLOAT);
KNOWN_AS(jast_double_t, DOUBLE);
KNOWN_AS(jast_array_t, ARRAY);
KNOWN_AS(jast_boolean_t, BOOLEAN);
KNOWN_AS(jast_null_t, NUL);
KNOWN_AS(jast_undefined_t, UNDEFINED);
KNOWN_AS(jast_function_t, FUNCTION);
typedef struct {
    JastType vtype;  // ← 类型标签
    union {
        double number;
        LPSTRING string;
        LPBOOLEAN boolean;
        LPARRAY array;
        LPOBJECT object;
        LPFUNCTION function;
        void* userdata;
    } as;
} JastValue;

typedef struct {
    
} jast_proto_t;

typedef struct {
    
}jast_object_t;

LPOBJECT alloc_object();
LPSTRING alloc_string();
LPINTERGER alloc_integer();
LPNUMBER alloc_number();
LPFLOAT alloc_float();
LPDOUBLE alloc_double();
LPARRAY alloc_array();

// Object protos
void Object_proto_function_set(LPOBJECT this_obj,LPSTRING key,LPOBJECT value);
LPOBJECT Object_proto_function_get(LPOBJECT this_obj,LPSTRING key);
LPSTRING Object_proto_function_toString();

// String protos
LPINTERGER String_proto_function_indexOf(LPSTRING);
LPSTRING String_proto_function_substring(LPSTRING);

struct jast_string{
};

typedef struct{

} jast_integer_t;

typedef struct {

 }jast_number_t;

typedef struct{

} jast_float_t;

typedef struct {

}jast_double_t;


typedef struct{

}jast_array_t;