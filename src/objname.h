/*
 * objname.h - Named object registry for stim2
 *
 * Allows objects to be referenced by name instead of integer id.
 *
 * Integration:
 *   1. Add to OBJ_LIST in stim2.h:
 *        void *nameInfo;
 *      And macro:
 *        #define OL_NAMEINFO(m) ((m)->nameInfo)
 *
 *   2. In interpreter init:
 *        OL_NAMEINFO(olist) = objNameInitCommands(interp, olist);
 *
 *   3. In commands, replace Tcl_GetInt pattern with:
 *        if ((id = resolveObjId(interp, OL_NAMEINFO(olist), argv[1], 
 *                               PolygonObjID, "polygon")) < 0)
 *            return TCL_ERROR;
 *
 *   4. In resetObjList:
 *        objNameClear(OL_NAMEINFO(olist));
 *
 *   Cleanup is automatic when interpreter is deleted.
 */

#ifndef OBJNAME_H
#define OBJNAME_H

#ifdef __cplusplus
extern "C" {
#endif
  
#include <tcl.h>
#include <stim2.h>

/*
 * Per-interpreter state for object naming.
 * Stored as void* in OBJ_LIST.nameInfo to avoid Tcl deps in stim2.h
 */
typedef struct ObjNameInfo {
    Tcl_HashTable nameToId;     /* name -> id */
    Tcl_HashTable idToName;     /* id -> name (for reverse lookup/cleanup) */
    OBJ_LIST *olist;            /* object list for this interpreter */
    int initialized;
} ObjNameInfo;

/*
 * Initialize the name registry (called automatically by objNameInitCommands).
 * Registers Tcl_CallWhenDeleted for automatic cleanup.
 */
ObjNameInfo *objNameInit(Tcl_Interp *interp, OBJ_LIST *olist);

/*
 * Initialize and register Tcl commands (objName, objByName, objNames, objNameClear).
 * Returns ObjNameInfo pointer - store this for use with resolveObjId.
 * Cleanup is automatic when interpreter is deleted (via Tcl_CallWhenDeleted).
 */
ObjNameInfo *objNameInitCommands(Tcl_Interp *interp, OBJ_LIST *olist);

/*
 * Clear all names (call from resetObjList, or use objNameClear Tcl command)
 */
void objNameClear(ObjNameInfo *info);

/*
 * C API for name management
 */
int objNameSet(ObjNameInfo *info, int id, const char *name);
int objNameGet(ObjNameInfo *info, const char *name);
const char *objIdGetName(ObjNameInfo *info, int id);
OBJ_LIST *objNameGetOlist(ObjNameInfo *info);

/*
 * resolveObjId - Resolve object identifier (integer id or string name)
 *
 * Parameters:
 *   interp   - Tcl interpreter (for error messages)
 *   info     - ObjNameInfo struct (from objNameInitCommands)
 *   arg      - String containing integer id or object name
 *   reqtype  - Required object type, or -1 for any type
 *   typename - Name of required type (for error messages), or NULL
 *
 * Returns:
 *   Object id on success, -1 on error (with interp result set)
 *
 * Example usage in commands:
 *
 *   // Any object type:
 *   if ((id = resolveObjId(interp, info, argv[1], -1, NULL)) < 0)
 *       return TCL_ERROR;
 *
 *   // Specific type required:
 *   if ((id = resolveObjId(interp, info, argv[1], PolygonObjID, "polygon")) < 0)
 *       return TCL_ERROR;
 */
int resolveObjId(Tcl_Interp *interp, ObjNameInfo *info, const char *arg,
                 int reqtype, const char *tname);

#ifdef __cplusplus
}
#endif

#endif /* OBJNAME_H */
