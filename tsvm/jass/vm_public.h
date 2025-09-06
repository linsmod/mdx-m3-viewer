#ifndef vm_public_h
#define vm_public_h

#include "../shared.h"
#include "../api/api_macros.h"

#define MAX_GROUP_SIZE 256
#define DEBUG_JASS 1

#define INDENT(depth) \
FOR_LOOP(i, depth) fprintf(stdout," ");

static __thread int depth = 0;

#define API_ALLOC(TYPE, NAME) TYPE *NAME = jass_newhandle(j, sizeof(TYPE), #NAME);

KNOWN_AS(jass_function, JASSFUNC);
KNOWN_AS(jass_type, JASSTYPE);
KNOWN_AS(jass_var, JASSVAR);
KNOWN_AS(jass_module, JASSMODULE);
KNOWN_AS(jass_context, JASSCONTEXT);
KNOWN_AS(vm_program, VMPROGRAM);
KNOWN_AS(jass_s, JASS);
KNOWN_AS(gtrigger_s, TRIGGER);
KNOWN_AS(gtriggercondition_s, TRIGGERCONDITION);
KNOWN_AS(gtriggeraction_s, TRIGGERACTION);

struct gtriggercondition_s {
    LPCJASSFUNC expr;
    LPTRIGGERCONDITION next;
};
struct gtriggeraction_s {
    LPCJASSFUNC func;
    LPTRIGGERACTION next;
};
struct gtrigger_s {
    LPTRIGGERACTION actions;
    LPTRIGGERCONDITION conditions;
    BOOL disabled;
};
typedef enum {
    CAMERA_FIELD_TARGET_DISTANCE,
    CAMERA_FIELD_FARZ,
    CAMERA_FIELD_ANGLE_OF_ATTACK,
    CAMERA_FIELD_FIELD_OF_VIEW,
    CAMERA_FIELD_ROLL,
    CAMERA_FIELD_ROTATION,
    CAMERA_FIELD_ZOFFSET,
} CAMERAFIELD;

typedef enum {
    UNIT_STATE_LIFE,
    UNIT_STATE_MAX_LIFE,
    UNIT_STATE_MANA,
    UNIT_STATE_MAX_MANA,
} UNITSTATE;

typedef DWORD (*LPJASSCFUNCTION)(LPJASS);

typedef enum {
    jasstype_integer,
    jasstype_real,
    jasstype_string,
    jasstype_boolean,
    jasstype_code,
    jasstype_handle,
    jasstype_cfunction,
} JASSTYPEID;

struct jass_module {
    LPCSTR name;
    LPJASSCFUNCTION func;
};

struct vm_program {
    HANDLE data;
    DWORD size;
};

struct jass_context {
    LPTRIGGER trigger;
    LPCJASSFUNC func;
};
LPJASS jass_newstate(void);
void jass_setnull(LPJASSVAR var);
void jass_close(LPJASS);
BOOL jass_dofile(LPJASS, LPCSTR);
BOOL jass_dofilenative(LPJASS, LPCSTR);
void jass_callbyname(LPJASS, LPCSTR, BOOL);
BOOL jass_dobuffer(LPJASS, LPSTR,LPCSTR);
LONG jass_checkinteger(LPJASS j, int index);
FLOAT jass_checknumber(LPJASS j, int index);
BOOL jass_checkboolean(LPJASS j, int index);
LPCSTR jass_checkstring(LPJASS j, int index);
LPCJASSFUNC jass_checkcode(LPJASS j, int index);
HANDLE jass_checkhandle(LPJASS j, int index, LPCSTR type);
BOOL jass_toboolean(LPJASS j, int index);
DWORD jass_call(LPJASS j, DWORD args);
void jass_runevents(LPJASS j);
JASSTYPEID jass_gettype(LPJASS j, int index);
DWORD jass_pushnull(LPJASS j);
DWORD jass_pushinteger(LPJASS j, LONG value);
DWORD jass_pushhandle(LPJASS j, HANDLE value, LPCSTR type);
DWORD jass_pushlighthandle(LPJASS j, HANDLE value, LPCSTR type);
DWORD jass_pushnumber(LPJASS j, FLOAT value);
DWORD jass_pushboolean(LPJASS j, BOOL value);
DWORD jass_pushstring(LPJASS j, LPCSTR value);
DWORD jass_pushstringlen(LPJASS j, LPCSTR value, DWORD len);
DWORD jass_pushfunction(LPJASS j, LPCJASSFUNC func);
DWORD jass_pushnullhandle(LPJASS j, LPCSTR type);
HANDLE jass_newhandle(LPJASS j, DWORD size, LPCSTR type);
LPCJASSCONTEXT jass_getcontext(LPJASS j);
BOOL jass_calltrigger(LPJASS j, LPTRIGGER trigger);
BOOL jass_popboolean(LPJASS j);
BOOL jass_evaluatetrigger(LPJASS j, LPTRIGGER trigger);
void jass_executetrigger(LPJASS j, LPTRIGGER trigger);
void jass_dumpstack(LPJASS j);

#endif
