/*
 * objname.c - Named object registry for stim2
 *
 * Allows objects to be referenced by name instead of integer id.
 * All obj-related commands can accept either an integer id or a string name.
 *
 * Uses per-interpreter state with automatic cleanup via Tcl_CallWhenDeleted.
 *
 * Usage:
 *   objName $id "player"       ;# register name for object
 *   objName $id ""             ;# clear name for object
 *   objByName "player"         ;# get id by name (rarely needed)
 *   objNames                   ;# list all named objects
 *
 *   scaleObj player 2.0 2.0    ;# use name directly in commands
 *   translateObj 42 1.0 0 0    ;# or use integer id as before
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <tcl.h>
#include <stim2.h>
#include "objname.h"

/********************************************************************/
/*                     CLEANUP CALLBACK                             */
/********************************************************************/

static void objNameInterpDeleted(ClientData clientData, Tcl_Interp *interp)
{
    ObjNameInfo *info = (ObjNameInfo *)clientData;
    Tcl_HashEntry *entry;
    Tcl_HashSearch search;
    
    if (!info) return;
    
    if (info->initialized) {
        /* Free strdup'd names in id->name table */
        entry = Tcl_FirstHashEntry(&info->idToName, &search);
        while (entry) {
            free((char *)Tcl_GetHashValue(entry));
            entry = Tcl_NextHashEntry(&search);
        }
        
        Tcl_DeleteHashTable(&info->nameToId);
        Tcl_DeleteHashTable(&info->idToName);
    }
    
    free(info);
}

/********************************************************************/
/*                     INITIALIZATION                               */
/********************************************************************/

ObjNameInfo *objNameInit(Tcl_Interp *interp, OBJ_LIST *olist)
{
    ObjNameInfo *info = (ObjNameInfo *)calloc(1, sizeof(ObjNameInfo));
    if (!info) return NULL;
    
    Tcl_InitHashTable(&info->nameToId, TCL_STRING_KEYS);
    Tcl_InitHashTable(&info->idToName, TCL_ONE_WORD_KEYS);
    info->olist = olist;
    info->initialized = 1;
    
    /* Register cleanup callback */
    Tcl_CallWhenDeleted(interp, objNameInterpDeleted, (ClientData)info);
    
    return info;
}

void objNameClear(ObjNameInfo *info)
{
    Tcl_HashEntry *entry;
    Tcl_HashSearch search;
    
    if (!info || !info->initialized) return;
    
    /* Free strdup'd names in id->name table */
    entry = Tcl_FirstHashEntry(&info->idToName, &search);
    while (entry) {
        free((char *)Tcl_GetHashValue(entry));
        entry = Tcl_NextHashEntry(&search);
    }
    
    Tcl_DeleteHashTable(&info->nameToId);
    Tcl_DeleteHashTable(&info->idToName);
    Tcl_InitHashTable(&info->nameToId, TCL_STRING_KEYS);
    Tcl_InitHashTable(&info->idToName, TCL_ONE_WORD_KEYS);
}

/********************************************************************/
/*                     CORE FUNCTIONS                               */
/********************************************************************/

/*
 * Register a name for an object id.
 * If name is empty or NULL, clears any existing name for this id.
 * If name already exists for different id, overwrites it.
 */
int objNameSet(ObjNameInfo *info, int id, const char *name)
{
    Tcl_HashEntry *entry;
    int newentry;
    char *oldname;
    
    if (!info || !info->initialized) return -1;
    
    /* First, clear any existing name for this id */
    entry = Tcl_FindHashEntry(&info->idToName, (char *)(intptr_t)id);
    if (entry) {
        oldname = (char *)Tcl_GetHashValue(entry);
        /* Remove from name->id table */
        Tcl_HashEntry *nameEntry = Tcl_FindHashEntry(&info->nameToId, oldname);
        if (nameEntry) {
            Tcl_DeleteHashEntry(nameEntry);
        }
        free(oldname);
        Tcl_DeleteHashEntry(entry);
    }
    
    /* If new name is empty, we're done (just clearing) */
    if (!name || !name[0]) {
        return 0;
    }
    
    /* If this name was used by another id, clear that mapping */
    entry = Tcl_FindHashEntry(&info->nameToId, name);
    if (entry) {
        int oldid = (int)(intptr_t)Tcl_GetHashValue(entry);
        Tcl_HashEntry *idEntry = Tcl_FindHashEntry(&info->idToName, (char *)(intptr_t)oldid);
        if (idEntry) {
            free((char *)Tcl_GetHashValue(idEntry));
            Tcl_DeleteHashEntry(idEntry);
        }
        Tcl_DeleteHashEntry(entry);
    }
    
    /* Add new name -> id mapping */
    entry = Tcl_CreateHashEntry(&info->nameToId, name, &newentry);
    Tcl_SetHashValue(entry, (ClientData)(intptr_t)id);
    
    /* Add reverse id -> name mapping */
    entry = Tcl_CreateHashEntry(&info->idToName, (char *)(intptr_t)id, &newentry);
    Tcl_SetHashValue(entry, strdup(name));

    /* Also update GR_NAME on the object itself */
    if (id >= 0 && id < OL_NOBJS(info->olist)) {
        GR_OBJ *obj = OL_OBJ(info->olist, id);
        if (name && name[0]) {
            strncpy(GR_NAME(obj), name, 63);
            GR_NAME(obj)[63] = '\0';
        } else {
            /* Clear - restore default? Or leave as-is? */
        }
    }
    
    return 0;
}

/*
 * Get object id by name.
 * Returns id if found, -1 if not found.
 */
int objNameGet(ObjNameInfo *info, const char *name)
{
    Tcl_HashEntry *entry;
    
    if (!info || !info->initialized) return -1;
    if (!name || !name[0]) return -1;
    
    entry = Tcl_FindHashEntry(&info->nameToId, name);
    if (entry) {
        return (int)(intptr_t)Tcl_GetHashValue(entry);
    }
    return -1;
}

/*
 * Get name for an object id.
 * Returns name if found, NULL if not found.
 */
const char *objIdGetName(ObjNameInfo *info, int id)
{
    Tcl_HashEntry *entry;
    
    if (!info || !info->initialized) return NULL;
    
    entry = Tcl_FindHashEntry(&info->idToName, (char *)(intptr_t)id);
    if (entry) {
        return (char *)Tcl_GetHashValue(entry);
    }
    return NULL;
}

/*
 * Get the OBJ_LIST from info struct
 */
OBJ_LIST *objNameGetOlist(ObjNameInfo *info)
{
    return info ? info->olist : NULL;
}

/********************************************************************/
/*                     OBJECT RESOLUTION                            */
/********************************************************************/

/*
 * resolveObjId - Resolve object identifier (integer id or string name)
 *
 * Parameters:
 *   interp   - Tcl interpreter (for error messages)
 *   info     - ObjNameInfo struct
 *   arg      - String containing integer id or object name
 *   reqtype  - Required object type, or -1 for any type
 *   tame     - Name of required type (for error messages), or NULL
 *
 * Returns:
 *   Object id on success, -1 on error (with interp result set)
 */
int resolveObjId(Tcl_Interp *interp, ObjNameInfo *info, const char *arg,
                 int reqtype, const char *tname)
{
    int id;
    OBJ_LIST *olist;
    
    if (!info) {
        Tcl_SetResult(interp, "object name registry not initialized", TCL_STATIC);
        return -1;
    }
    
    olist = info->olist;
    
    /* Try integer first (fast path, no hash lookup) */
    if (Tcl_GetInt(NULL, arg, &id) == TCL_OK) {
        /* Validate range */
        if (id < 0 || id >= OL_NOBJS(olist)) {
            Tcl_AppendResult(interp, "object id out of range: ", arg, NULL);
            return -1;
        }
        /* Check type if required */
        if (reqtype >= 0 && GR_OBJTYPE(OL_OBJ(olist, id)) != reqtype) {
            if (tname) {
                Tcl_AppendResult(interp, "object is not a ", tname, ": ", arg, NULL);
            } else {
                Tcl_AppendResult(interp, "object type mismatch: ", arg, NULL);
            }
            return -1;
        }
        return id;
    }
    
    /* Try name lookup */
    id = objNameGet(info, arg);
    if (id >= 0) {
        /* Validate id still valid (object might have been deleted/reset) */
        if (id >= OL_NOBJS(olist)) {
            Tcl_AppendResult(interp, "object no longer valid: ", arg, NULL);
            return -1;
        }
        /* Check type if required */
        if (reqtype >= 0 && GR_OBJTYPE(OL_OBJ(olist, id)) != reqtype) {
            if (tname) {
                Tcl_AppendResult(interp, "object is not a ", tname, ": ", arg, NULL);
            } else {
                Tcl_AppendResult(interp, "object type mismatch: ", arg, NULL);
            }
            return -1;
        }
        return id;
    }
    
    Tcl_AppendResult(interp, "unknown object: ", arg, NULL);
    return -1;
}

/********************************************************************/
/*                     TYPE RESOLUTION                              */
/********************************************************************/

/*
 * Resolve a type name string to a type id using core grobj registry.
 * Returns typeId on success, -1 if not found.
 */
static int resolveTypeName(const char *name)
{
    return gobjFindType(name);
}

/********************************************************************/
/*                     TCL COMMANDS                                 */
/********************************************************************/

/*
 * objName id ?name?
 *   With name: register name for object, returns name
 *   Without name: returns current name for object (or empty string)
 */
static int objNameCmd(ClientData clientData, Tcl_Interp *interp,
                      int argc, char *argv[])
{
    ObjNameInfo *info = (ObjNameInfo *)clientData;
    OBJ_LIST *olist = info->olist;
    int id;
    const char *name;
    
    if (argc < 2 || argc > 3) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " id ?name?", NULL);
        return TCL_ERROR;
    }
    
    /* Get the object id (must be integer here, can't use name to set name) */
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) {
        return TCL_ERROR;
    }
    
    if (id < 0 || id >= OL_NOBJS(olist)) {
        Tcl_AppendResult(interp, "object id out of range: ", argv[1], NULL);
        return TCL_ERROR;
    }
    
    if (argc == 3) {
        /* Set name */
        objNameSet(info, id, argv[2]);
        Tcl_SetResult(interp, argv[2], TCL_VOLATILE);
    } else {
        /* Get name */
        name = objIdGetName(info, id);
        if (name) {
            Tcl_SetResult(interp, (char *)name, TCL_VOLATILE);
        } else {
            Tcl_SetResult(interp, "", TCL_STATIC);
        }
    }
    
    return TCL_OK;
}

/*
 * objByName name
 *   Returns object id for given name, or error if not found
 */
static int objByNameCmd(ClientData clientData, Tcl_Interp *interp,
                        int argc, char *argv[])
{
    ObjNameInfo *info = (ObjNameInfo *)clientData;
    int id;
    
    if (argc != 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " name", NULL);
        return TCL_ERROR;
    }
    
    id = objNameGet(info, argv[1]);
    if (id < 0) {
        Tcl_AppendResult(interp, "unknown object name: ", argv[1], NULL);
        return TCL_ERROR;
    }
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    return TCL_OK;
}

/*
 * objNames
 *   Returns list of all named objects as {name id name id ...}
 */
static int objNamesCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[])
{
    ObjNameInfo *info = (ObjNameInfo *)clientData;
    Tcl_HashEntry *entry;
    Tcl_HashSearch search;
    Tcl_Obj *listObj;
    
    if (argc != 1) {
        Tcl_AppendResult(interp, "usage: ", argv[0], NULL);
        return TCL_ERROR;
    }
    
    listObj = Tcl_NewListObj(0, NULL);
    
    if (info && info->initialized) {
        entry = Tcl_FirstHashEntry(&info->nameToId, &search);
        while (entry) {
            const char *name = Tcl_GetHashKey(&info->nameToId, entry);
            int id = (int)(intptr_t)Tcl_GetHashValue(entry);
            
            Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj(name, -1));
            Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(id));
            
            entry = Tcl_NextHashEntry(&search);
        }
    }
    
    Tcl_SetObjResult(interp, listObj);
    return TCL_OK;
}

/*
 * objNameClear - Tcl command to clear all names (called by resetObjList)
 */
static int objNameClearCmd(ClientData clientData, Tcl_Interp *interp,
                           int argc, char *argv[])
{
    ObjNameInfo *info = (ObjNameInfo *)clientData;
    objNameClear(info);
    return TCL_OK;
}

/*
 * objFind ?-type typename? ?-match pattern? ?-names?
 *
 *   Without -match: iterates full object list (finds unnamed objects too)
 *   With -match: only searches named objects (unnamed have nothing to match)
 *   With -names: returns {name id name id ...} instead of {id id ...}
 */
static int objFindCmd(ClientData clientData, Tcl_Interp *interp,
                      int argc, char *argv[])
{
    ObjNameInfo *info = (ObjNameInfo *)clientData;
    OBJ_LIST *olist;
    int typeFilter = -1;
    int groupFilter = -1;
    const char *matchPattern = NULL;
    int returnNames = 0;
    Tcl_Obj *listObj;
    int i;
    
    if (!info || !info->initialized) {
        Tcl_SetResult(interp, "object name registry not initialized", TCL_STATIC);
        return TCL_ERROR;
    }
    
    olist = info->olist;
    
    /* Parse arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-type") == 0) {
            if (++i >= argc) {
                Tcl_AppendResult(interp, "-type requires a value", NULL);
                return TCL_ERROR;
            }
            typeFilter = resolveTypeName(argv[i]);
            if (typeFilter < 0) {
                /* Try as integer for forward compatibility */
                if (Tcl_GetInt(NULL, argv[i], &typeFilter) != TCL_OK) {
                    Tcl_AppendResult(interp, "unknown object type: ", argv[i],
                                     "\nregistered types:", NULL);
                    for (int t = 0; t < gobjNumTypes(); t++) {
                        char *tn = gobjTypeName(t);
                        if (tn) Tcl_AppendResult(interp, " ", tn, NULL);
                    }
                    return TCL_ERROR;
                }
            }
        } else if (strcmp(argv[i], "-match") == 0) {
            if (++i >= argc) {
                Tcl_AppendResult(interp, "-match requires a pattern", NULL);
                return TCL_ERROR;
            }
            matchPattern = argv[i];
        } else if (strcmp(argv[i], "-group") == 0) {
            if (++i >= argc) {
                Tcl_AppendResult(interp, "-group requires a value", NULL);
                return TCL_ERROR;
            }
            if (Tcl_GetInt(interp, argv[i], &groupFilter) != TCL_OK) {
                return TCL_ERROR;
            }
            if (groupFilter < 0 || groupFilter >= OGL_NGROUPS(GList)) {
                Tcl_AppendResult(interp, "group out of range: ", argv[i], NULL);
                return TCL_ERROR;
            }
        } else if (strcmp(argv[i], "-names") == 0) {
            returnNames = 1;
        } else {
            Tcl_AppendResult(interp, "unknown option: ", argv[i],
                             "\nusage: objFind ?-type typename? ?-group num? ?-match pattern? ?-names?",
                             NULL);
            return TCL_ERROR;
        }
    }
    
    listObj = Tcl_NewListObj(0, NULL);
    
    if (matchPattern) {
        /*
         * With -match: iterate named objects only.
         * Unnamed objects have nothing to match against.
         */
        Tcl_HashEntry *entry;
        Tcl_HashSearch search;
        
        entry = Tcl_FirstHashEntry(&info->nameToId, &search);
        while (entry) {
            const char *name = Tcl_GetHashKey(&info->nameToId, entry);
            int id = (int)(intptr_t)Tcl_GetHashValue(entry);
            
            /* Check glob match */
            if (Tcl_StringMatch(name, matchPattern)) {
                /* Check type filter if specified */
                if (typeFilter >= 0) {
                    if (id >= OL_NOBJS(olist)) goto next;
                    if (GR_OBJTYPE(OL_OBJ(olist, id)) != typeFilter) goto next;
                }
                /* Check group filter if specified */
                if (groupFilter >= 0) {
                    OBJ_GROUP *g = OGL_GROUP(GList, groupFilter);
                    int found = 0, gi;
                    for (gi = 0; gi < OG_NOBJS(g); gi++) {
                        if (OG_OBJID(g, gi) == id) { found = 1; break; }
                    }
                    if (!found) goto next;
                }
                
                if (returnNames) {
                    Tcl_ListObjAppendElement(interp, listObj,
                                             Tcl_NewStringObj(name, -1));
                }
                Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(id));
            }
        next:
            entry = Tcl_NextHashEntry(&search);
        }
    } else if (groupFilter >= 0) {
        /*
         * With -group: iterate only that group's objects.
         */
        OBJ_GROUP *g = OGL_GROUP(GList, groupFilter);
        int nobjs = OG_NOBJS(g);
        
        for (i = 0; i < nobjs; i++) {
            int id = OG_OBJID(g, i);
            GR_OBJ *obj = OL_OBJ(olist, id);
            
            /* Skip inactive/freed objects */
            if (!obj) continue;
            
            /* Check type filter if specified */
            if (typeFilter >= 0 && GR_OBJTYPE(obj) != typeFilter) continue;
            
            if (returnNames) {
                const char *name = objIdGetName(info, id);
                Tcl_ListObjAppendElement(interp, listObj,
                    Tcl_NewStringObj(name ? name : "", -1));
            }
            Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(id));
        }
    } else {
        /*
         * Without -match or -group: iterate full object list.
         * This finds unnamed objects too.
         */
        int nobjs = OL_NOBJS(olist);
        
        for (i = 0; i < nobjs; i++) {
            GR_OBJ *obj = OL_OBJ(olist, i);
            
            /* Skip inactive/freed objects */
            if (!obj) continue;
            
            /* Check type filter if specified */
            if (typeFilter >= 0 && GR_OBJTYPE(obj) != typeFilter) continue;
            
            if (returnNames) {
                const char *name = objIdGetName(info, i);
                Tcl_ListObjAppendElement(interp, listObj,
                    Tcl_NewStringObj(name ? name : "", -1));
            }
            Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(i));
        }
    }
    
    Tcl_SetObjResult(interp, listObj);
    return TCL_OK;
}

/*
 * objTypes
 *   Returns list of registered type names as {name typeid name typeid ...}
 */
static int objTypesCmd(ClientData clientData, Tcl_Interp *interp,
                       int argc, char *argv[])
{
    Tcl_Obj *listObj;
    int i, n;
    
    if (argc != 1) {
        Tcl_AppendResult(interp, "usage: ", argv[0], NULL);
        return TCL_ERROR;
    }
    
    listObj = Tcl_NewListObj(0, NULL);
    n = gobjNumTypes();
    
    for (i = 0; i < n; i++) {
        char *name = gobjTypeName(i);
        if (name) {
            Tcl_ListObjAppendElement(interp, listObj,
                Tcl_NewStringObj(name, -1));
            Tcl_ListObjAppendElement(interp, listObj,
                Tcl_NewIntObj(i));
        }
    }
    
    Tcl_SetObjResult(interp, listObj);
    return TCL_OK;
}

/********************************************************************/
/*                     COMMAND REGISTRATION                         */
/********************************************************************/

/*
 * Initialize and register commands.
 * Returns the ObjNameInfo pointer (store this for use with resolveObjId).
 * Cleanup is automatic when interpreter is deleted.
 */
ObjNameInfo *objNameInitCommands(Tcl_Interp *interp, OBJ_LIST *olist)
{
    ObjNameInfo *info = objNameInit(interp, olist);
    if (!info) return NULL;
    
    Tcl_CreateCommand(interp, "objName",
                      (Tcl_CmdProc *)objNameCmd,
                      (ClientData)info, (Tcl_CmdDeleteProc *)NULL);
    
    Tcl_CreateCommand(interp, "objByName",
                      (Tcl_CmdProc *)objByNameCmd,
                      (ClientData)info, (Tcl_CmdDeleteProc *)NULL);
    
    Tcl_CreateCommand(interp, "objNames",
                      (Tcl_CmdProc *)objNamesCmd,
                      (ClientData)info, (Tcl_CmdDeleteProc *)NULL);
    
    Tcl_CreateCommand(interp, "objNameClear",
                      (Tcl_CmdProc *)objNameClearCmd,
                      (ClientData)info, (Tcl_CmdDeleteProc *)NULL);
    
    Tcl_CreateCommand(interp, "objFind",
                      (Tcl_CmdProc *)objFindCmd,
                      (ClientData)info, (Tcl_CmdDeleteProc *)NULL);
    
    Tcl_CreateCommand(interp, "objTypes",
                      (Tcl_CmdProc *)objTypesCmd,
                      (ClientData)info, (Tcl_CmdDeleteProc *)NULL);
    
    return info;
}
