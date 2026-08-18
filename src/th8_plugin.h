/*
 * th8_plugin.h -- Plugin system for TH8.
 *
 * A plugin is a restricted form of extension that provides commands
 * via a static table.  Unlike full extensions (which use _Init and
 * _Unload entry points), plugins are registered programmatically
 * via Th8_RegisterPlugin.  Each command gets a unique token that
 * survives rename, enabling reliable unregistration.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_PLUGIN_H
#define TH8_PLUGIN_H

/*
 * NOTE: This header does not include other project headers.
 * Each .c file must include th8.h (for TH8_API, Th8_Interp,
 * th8_int64_t, Th8_CommandProc) before this.  Core translation
 * units also include th8_int.h first, which defines TH8_INTERNAL
 * with hidden visibility.
 *
 * The generated public stub table (th8Decls.h) pulls this header
 * in for the plugin API types (Th8_CommandEntry,
 * Th8_GetCommandsProc) WITHOUT th8_int.h, so provide a benign
 * fallback for TH8_INTERNAL here -- mirroring th8_hash.h's TH8_API
 * fallback.  The #ifndef guard (matching th8_int.h's own guard at
 * line 43) means the core hidden-visibility definition always wins
 * because th8_int.h is included first there; only stubs-consumer
 * builds fall back to a plain `extern` for the internal helper
 * declarations, which is harmless (they are declarations only).
 */
#ifndef TH8_INTERNAL
#  define TH8_INTERNAL extern
#endif

/*
 *----------------------------------------------------------------------
 *
 * Th8_CommandEntry --
 *
 *	Describes a single command provided by a plugin.  The plugin
 *	fills in nVersion, zName, and xProc; token must be set to 0.
 *	After registration, the core writes a unique token value.
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_CommandEntry {
    th8_int64_t nVersion; /* Struct version (must be 1). */
    th8_uint64_t token;  /* Managed by core; plugin sets to 0. */
    const char *zName;  /* Command name (NUL-terminated). */
    Th8_CommandProc xProc; /* Command implementation function. */
} Th8_CommandEntry;

/*
 * Th8_GetCommandsProc --
 *
 *	Plugin callback that returns the static command table.
 *
 *	When pCommand is NULL, the plugin sets *pnCommand to the
 *	number of commands it provides.  When pCommand points to an
 *	array of that size, the plugin fills in each entry (setting
 *	token to 0).
 *
 *	WARNING: This function must NOT perform any actions other
 *	than populating the output parameters.  It must not call
 *	interpreter APIs, allocate memory, open files, or have any
 *	other side effects.
 */

typedef int (*Th8_GetCommandsProc)(
    Th8_CommandEntry *pCommand, /* OUT: array to fill, or NULL. */
    int *pnCommand);  /* IN/OUT: array size / command count. */


/*
 *----------------------------------------------------------------------
 *
 * Public API.
 *
 *----------------------------------------------------------------------
 */

/*
 * Th8_RegisterPlugin --
 *
 *	Register a named plugin with the interpreter.  Calls the
 *	plugin's GetCommands callback to obtain the command table,
 *	then creates all listed commands.  Each command receives a
 *	unique token for rename-resistant unregistration.
 *
 *	Returns TH8_OK on success, TH8_ERROR if the plugin name
 *	is already registered or any command creation fails.
 */
TH8_API int Th8_RegisterPlugin(
    Th8_Interp *interp,
    const char *zName,
    Th8_GetCommandsProc xGetCommands);

/*
 * Th8_UnregisterPlugin --
 *
 *	Remove all commands registered by the named plugin, even
 *	if they have been renamed.  Commands that were deleted or
 *	replaced by the user are silently skipped.
 */
TH8_API int Th8_UnregisterPlugin(Th8_Interp *interp, const char *zName);

/*
 * Th8_ListAppendPlugins --
 *
 *	Append the names of all registered plugins to a Tcl list.
 */
TH8_API int Th8_ListAppendPlugins(Th8_Interp *interp, char **pz, size_t *pn);

/*
 * Accessor functions (defined in th8_core.c).
 */
TH8_INTERNAL void *th8GetPluginList(Th8_Interp *interp);
TH8_INTERNAL void th8SetPluginList(Th8_Interp *interp, void *p);
TH8_INTERNAL int
th8PluginRegistered(Th8_Interp *interp, const char *zName, size_t nName);
TH8_INTERNAL th8_uint64_t th8NextCmdToken(Th8_Interp *interp);
TH8_INTERNAL void
th8SetCmdToken(Th8_Interp *interp, const char *zName, th8_uint64_t token);
TH8_INTERNAL th8_uint64_t
th8GetCmdToken(Th8_Interp *interp, const char *zName);

/*
 * Plugin cleanup (called from Th8_DeleteInterp).
 */
void th8PluginCleanup(Th8_Interp *interp);

/*
 * Built-in plugin GetCommands functions.
 * Each is gated on its compile-time define so that disabled
 * plugins produce no unresolved symbols.
 */
#if defined(TH8_PLUGIN_BINARY)
int th8BinaryGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_CONTROL)
int th8ControlGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_EVENTS)
int th8EventsGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)
int th8CryptoGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
int th8HarpyGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_EXPRESSIONS)
int th8ExpressionsGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_EXTENSIBILITY)
int th8ExtensibilityGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_FILE_SYSTEMS)
int th8FilesystemsGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_FORMATTING)
int th8FormattingGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_INTROSPECTION)
int th8IntrospectionGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_IO)
int th8IoGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_LISTS)
int th8ListsGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_LOOPING)
int th8LoopingGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_MANAGEMENT)
int th8ManagementGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_PROCEDURES)
int th8ProceduresGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_ENABLE_REGEXP)
int th8RegexpGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_STRINGS)
int th8StringsGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_TIMEKEEPING)
int th8TimekeepingGetCommands(Th8_CommandEntry *, int *);
#endif
#if defined(TH8_PLUGIN_VARIABLES)
int th8VariablesGetCommands(Th8_CommandEntry *, int *);
#endif

/*
 * Harpy-provided clock subcommand callbacks.
 * Defined in th8_harpy.c, referenced by th8_timekeeping.c
 * in the clock ensemble subcommand table.
 */
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
int th8HarpyClockNtpCommand(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl);
int th8HarpyClockHttpsCommand(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl);
#endif

#endif /* TH8_PLUGIN_H */
