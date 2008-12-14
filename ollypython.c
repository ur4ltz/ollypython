#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <shlwapi.h>
#include <Python.h>

#include "plugin.h"

void init_ollyapi(void);

HINSTANCE hinst;
HWND hwmain;

static int initialized = 0;

#ifdef ENABLE_PYTHON_PROFILING
#include "compile.h"
#include "frameobject.h"

int tracefunc(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg)
{
	PyObject *str;

	/* Catch line change events. */
	/* Print the filename and line number */	
	if (what == PyTrace_LINE)
	{
		str = PyObject_Str(frame->f_code->co_filename);
		if (str)
		{
			Addtolist(0, -1, "PROFILING: %s:%d", PyString_AsString(str), frame->f_lineno);
			Py_DECREF(str);
		}
	}
	return 0;
}
#endif

int ExecFile(char *FileName)
{
	PyObject* PyFileObject = PyFile_FromString(FileName, "r");

	if (!PyFileObject) 
	{
		return 0;
	}

	if (PyRun_SimpleFile(PyFile_AsFile(PyFileObject), FileName) == 0)
 	{
		Py_DECREF(PyFileObject);
		return 1;
	}
	else
	{
		Py_DECREF(PyFileObject);
		return 0;
	}
}

BOOL CheckFile(char *filename)
{
    char path[MAX_PATH];

    GetModuleFileName(hinst, path, MAX_PATH);
    PathRemoveFileSpec(path);
    strncat(path, "\\python\\", 8);
    strcat(path, filename);

    return PathFileExists(path);
}

BOOL OllyPython_Init(void)
{
    char initfile[MAX_PATH];
    char tmp[MAX_PATH+16];
    BOOL result = 1;

    if (initialized == 1)
    {
        return TRUE;
    }

    Addtolist(0, 0, "OllyPython");

    result &= CheckFile("init.py");
    result &= CheckFile("ollyapi.py");
    result &= CheckFile("ollyutils.py");

    if (!result)
    {
        Addtolist(0, -1, "  Could not locate Python scripts");
        return FALSE;
    }

    Py_Initialize();

    if (!Py_IsInitialized())
    {
        Addtolist(0, -1, "  Could not initialize Python");
        return FALSE;
    }

    init_ollyapi();

    GetModuleFileName(hinst, initfile, MAX_PATH);
    PathRemoveFileSpec(initfile);
    strncat(initfile, "\\python", 7);

    snprintf(tmp, MAX_PATH+16, "OLLYPYTHON_PATH=\"%s\"", initfile);
    PyRun_SimpleString(tmp);

    strncat(initfile, "\\init.py", 8);

    if (!ExecFile(initfile))
    {
        Addtolist(0, -1, "  Could not run init.py");
        return FALSE;
    }

#ifdef ENABLE_PYTHON_PROFILING
    PyEval_SetTrace(tracefunc, NULL);
#endif

    initialized = 1;

    return TRUE;
}

void OllyPython_Destroy(void)
{
    Py_Finalize();

    initialized = 0;
}

void OllyPython_RunScript(char *script)
{
    char userscript[MAX_PATH];
    char slashpath[MAX_PATH+1];
    char statement[MAX_PATH+13];
    int validfile;
    char *scriptpath;
    int i;

    userscript[0] = '\0';

    if (script)
    {
        scriptpath = script;
    }
    else
    {
        scriptpath = userscript;

        validfile = Browsefilename("title", scriptpath, ".py", 1);

        if (!validfile)
        {
            return;
        }
    }

    for (i=0; scriptpath[i]; i++)
    {
        if (scriptpath[i] == '\\')
        {
            slashpath[i] = '/';
        }
        else
        {
            slashpath[i] = scriptpath[i];
        }
    }

    slashpath[i] = '\0';

    snprintf(statement, sizeof(statement), "runscript(\"%s\")", slashpath);
    PyRun_SimpleString(statement);
    
    if (PyErr_Occurred())
    {
        PyErr_Print();
    }
}

BOOL WINAPI DllEntryPoint(HINSTANCE hi,DWORD reason,LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        hinst = hi;

    return TRUE;
};

extc int  _export cdecl ODBG_Plugindata(char shortname[32])
{
    strcpy(shortname,"OllyPython");
    return PLUGIN_VERSION;
}

extc int  _export cdecl ODBG_Plugininit(int ollydbgversion,HWND hw,ulong *features)
{
    if (ollydbgversion < PLUGIN_VERSION)
        return -1;

    hwmain = hw;

    if (OllyPython_Init())
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

extc void _export cdecl ODBG_Pluginmainloop(DEBUG_EVENT *debugevent)
{

}

extc void _export cdecl ODBG_Pluginsaveudd(t_module *pmod,int ismainmodule)
{

}

extc int  _export cdecl ODBG_Pluginuddrecord(t_module *pmod,int ismainmodule, ulong tag,ulong size,void *data)
{
    return 0;
}

extc int  _export cdecl ODBG_Pluginmenu(int origin,char data[4096],void *item)
{
    switch (origin)
    {
        case PM_MAIN:
            strcpy(data, "0 Python &file...,");
            //strcat(data, "1 Python &command...|");
            strcat(data, "2 &About");
            return 1;
        default:
            return 0;
    }
}

extc void _export cdecl ODBG_Pluginaction(int origin,int action,void *item)
{
    switch (origin)
    {
        case PM_MAIN:
            switch (action)
            {
                case 0:
                    OllyPython_RunScript(NULL);
                    break;
                case 1:
                    MessageBox(hwmain, "Python command", "Python command", MB_OK|MB_ICONINFORMATION);
                    break;
                case 2:
                    MessageBox(hwmain, "OllyPython", "About OllyPython", MB_OK|MB_ICONINFORMATION);
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

extc int  _export cdecl ODBG_Pluginshortcut(int origin,int ctrl,int alt,int shift,int key,void *item)
{
    PyObject *shortcuts;
    int i;
    int shortcut_size;
    PyObject *result;
    PyObject *func;

    shortcuts = PyObject_GetAttrString(NULL, "ollypython_shortcuts");
    shortcut_size = PyList_Size(shortcuts);

    for (i = 0; i < shortcut_size; i++)
    {
        func = PyList_GetItem(shortcuts, i);
        result = PyObject_CallObject(func, NULL);
        if (PyInt_AsLong(result) == 1)
        {
            /* The shortcut was handled */
            return 1;
        }
    }

    return 0;
}

extc void _export cdecl ODBG_Pluginreset(void)
{

}

extc int  _export cdecl ODBG_Pluginclose(void)
{
    return 0;
}

extc void _export cdecl ODBG_Plugindestroy(void)
{
    OllyPython_Destroy();
}

extc int  _export cdecl ODBG_Paused(int reason,t_reg *reg)
{
//    MessageBox(hwmain, "ODBG_Paused", "ODBG_Paused", MB_OK|MB_ICONINFORMATION);

    return 0;
}

extc int  _export cdecl ODBG_Pausedex(int reasonex,int dummy,t_reg *reg,DEBUG_EVENT *debugevent)
{
//    MessageBox(hwmain, "ODBG_Pausedex", "ODBG_Pausedex", MB_OK|MB_ICONINFORMATION);

    return 0;
}

extc int  _export cdecl ODBG_Plugincmd(int reason,t_reg *reg,char *cmd)
{
//    MessageBox(hwmain, "ODBG_Plugincmd", "ODBG_Plugincmd", MB_OK|MB_ICONINFORMATION);

    return 0;
}
