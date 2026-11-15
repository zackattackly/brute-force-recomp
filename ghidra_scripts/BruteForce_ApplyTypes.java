// Defines structures for Brute Force's main objects (Character, World, the renderer and its
// render-state cache, the data-object base) from the field offsets documented in the README,
// and applies them to the global pointers and to the "this" parameter of the matching functions,
// so the decompiler shows field names (character->stamina) instead of offsets (+0x490).
// Run after BruteForce_ApplyNames.java. Safe to run more than once (types are replaced).
//@category BruteForce
//@description Create Character / World / Renderer structures and apply them (Milestone 66).
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.pcode.HighFunctionDBUtil;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.symbol.SourceType;

public class BruteForce_ApplyTypes extends GhidraScript {

    private DataTypeManager dtm;
    private static final CategoryPath CAT = new CategoryPath("/BruteForce");
    private final DataType FLT = FloatDataType.dataType;
    private final DataType INT = IntegerDataType.dataType;
    private final DataType UINT = UnsignedIntegerDataType.dataType;
    private final DataType BYTE = ByteDataType.dataType;
    private final DataType PTR = PointerDataType.dataType;

    private StructureDataType st(String name, int size, String desc) {
        StructureDataType s = new StructureDataType(CAT, name, size, dtm);
        s.setDescription(desc);
        return s;
    }

    private void f(StructureDataType s, int off, DataType t, String name, String comment) {
        s.replaceAtOffset(off, t, t.getLength(), name, comment);
    }

    private DataType add(StructureDataType s) {
        return dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER);
    }

    private DataType ptr(DataType t) {
        return dtm.getPointer(t);
    }

    @Override
    public void run() throws Exception {
        dtm = currentProgram.getDataTypeManager();

        // ---- small math types
        StructureDataType vec3 = st("Vec3", 12, "x, y, z");
        f(vec3, 0, FLT, "x", null); f(vec3, 4, FLT, "y", null); f(vec3, 8, FLT, "z", null);
        DataType Vec3 = add(vec3);
        StructureDataType mat3 = st("Mat3", 36, "3x3 rotation, row major");
        f(mat3, 0, new ArrayDataType(FLT, 9, 4, dtm), "m", null);
        DataType Mat3 = add(mat3);

        // ---- motion record and character type (only the fields used by Character code)
        StructureDataType motion = st("MotionRecord", 0x260, "A character type's motion entry (MT_* id at +0).");
        f(motion, 0x000, INT, "motionType", "MT_* enum");
        f(motion, 0x25C, FLT, "staminaCost", "stamina used while this motion plays");
        DataType Motion = add(motion);
        DataType MotionP = ptr(Motion);

        StructureDataType ctype = st("CharacterType", 0x2C8, "Character type (archetype) fields used by Character code; size not known.");
        f(ctype, 0x220, FLT, "hitMultiplierBase", "base damage multiplier (Character_GetHitMultiplier)");
        f(ctype, 0x2A8, UINT, "flags", "bit 0: instances allowed");
        f(ctype, 0x2B8, MotionP, "tiredMotion", "used when stamina is under 5%");
        f(ctype, 0x2BC, MotionP, "standMotion", null);
        f(ctype, 0x2C0, MotionP, "standMotionA", null);
        f(ctype, 0x2C4, MotionP, "standMotionB", null);
        add(ctype);

        // ---- Character (0x860 bytes, Character_ctor)
        StructureDataType ch = st("Character", 0x860, "Character instance (Character_ctor, 0x860 bytes). Fields from Milestones 9-30; most names inferred.");
        f(ch, 0x000, PTR, "vtable", "g_vt_Character (114 methods)");
        f(ch, 0x048, PTR, "combatTargetVtable", "CombatTarget sub-object (g_vt_CharacterCombatTarget); Character_TakeDamage's this");
        f(ch, 0x1FC, MotionP, "motionCurrent", null);
        f(ch, 0x200, MotionP, "motionRequested", null);
        f(ch, 0x214, MotionP, "motionTransitionFrom", null);
        f(ch, 0x218, MotionP, "motionTransitionTo", null);
        f(ch, 0x240, new ArrayDataType(MotionP, 3, 4, dtm), "motionOverlays", null);
        f(ch, 0x261, BYTE, "motionFlags", "set by Character_UpdateTurning when idle/stopped");
        f(ch, 0x434, FLT, "yaw", "current heading (radians)");
        f(ch, 0x490, FLT, "stamina", "current-stamina");
        f(ch, 0x494, FLT, "maxStamina", "max-stamina");
        f(ch, 0x4A0, FLT, "targetYaw", "Character_TurnBy");
        f(ch, 0x568, UINT, "hitMultiplierMap", "map of per-body-part damage multipliers (start)");
        f(ch, 0x574, INT, "standVariantA", null);
        f(ch, 0x578, INT, "standVariantB", null);
        f(ch, 0x580, FLT, "standTimer", "when it runs out, Character_PickStandMotion");
        f(ch, 0x5A8, INT, "frameCountdownActive", null);
        f(ch, 0x5AC, INT, "frameCountdown", "ticks");
        f(ch, 0x5BC, Mat3, "aimMatrix", null);
        f(ch, 0x5F4, FLT, "aimPitch", "clamped to +/-60 degrees");
        f(ch, 0x5F8, FLT, "turnInput", null);
        f(ch, 0x5FC, FLT, "turnRate", "radians per second (Character_TurnBy)");
        f(ch, 0x748, UINT, "lockFlags", "bit 3: motion requests refused");
        f(ch, 0x7A8, INT, "zoomLevel", null);
        f(ch, 0x7BC, Vec3, "viewOffset", "local eye offset (Character_GetViewPosition)");
        f(ch, 0x7C8, FLT, "zoomTimer", null);
        f(ch, 0x7DC, FLT, "motionTimer", null);
        f(ch, 0x7EC, FLT, "turnSpeed", null);
        f(ch, 0x7F4, PTR, "timedEvents", "list; entry timer at +0x10");
        f(ch, 0x830, PTR, "controller", "AI controller (AIController_ctor) or player");
        f(ch, 0x010, INT, "lastUpdateTick", "GameObject: World_UpdateObjects skips objects already updated this tick");
        f(ch, 0x060, PTR, "shield", "GameObject: combat target +0x18 (GameObject_CreateShield)");
        f(ch, 0x55C, PTR, "damageModifiers", "list; each applied to outgoing damage (Weapon_RollDamage)");
        f(ch, 0x5A4, PTR, "shooterComponent", "vtbl+0xEC gives a damage factor (Weapon_RollDamage)");
        f(ch, 0x754, FLT, "damageFactor", "multiplies the damage of shots this character fires");
        DataType Character = add(ch);

        // ---- World (0xC84 bytes, World_ctor) - see README Milestone 14
        StructureDataType w = st("World", 0xC84, "The loaded level (World_ctor, 0xC84 bytes). Subsystem pointers from Milestone 14.");
        f(w, 0x004, PTR, "requestQueue", null);
        f(w, 0x040, PTR, "objectListHead", null);
        f(w, 0x044, INT, "objectCount", null);
        f(w, 0x054, PTR, "objectCursor", "lets objects be removed during World_UpdateObjects");
        f(w, 0x0F4, PTR, "navObjects", "master list of paths, triggers and world points");
        f(w, 0x1F4, PTR, "subsystem_1F4", null);
        f(w, 0x1F8, PTR, "subsystem_1F8", "AI-side");
        f(w, 0x1FC, PTR, "projectiles", "ProjectileManager");
        f(w, 0x200, PTR, "subsystem_200", null);
        f(w, 0x20C, PTR, "sceneScripts", "SceneScriptManager");
        f(w, 0xA40, PTR, "subsystem_A40", null);
        f(w, 0xA44, PTR, "staticObjects", null);
        f(w, 0xA48, PTR, "subsystem_A48", null);
        f(w, 0xA4C, PTR, "triggers", "OT_TRIGGER list");
        f(w, 0xA58, PTR, "cameraSystem", null);
        f(w, 0xB60, INT, "tickCount", null);
        f(w, 0xB68, PTR, "paths", "OT_PATH list");
        f(w, 0xB80, PTR, "effects", null);
        f(w, 0xBD8, PTR, "worldMaterials", null);
        f(w, 0xBDC, PTR, "squads", null);
        f(w, 0xBE0, PTR, "subsystem_BE0", null);
        f(w, 0xBE4, PTR, "viewManager", "one view and camera per player character");
        f(w, 0xBE8, PTR, "subsystem_BE8", null);
        f(w, 0xBEC, PTR, "playerManager", null);
        f(w, 0xBF0, PTR, "subsystem_BF0", null);
        f(w, 0xBF4, PTR, "messageSystem", null);
        f(w, 0xC04, PTR, "worldSound", null);
        DataType World = add(w);

        // ---- render-state cache (Milestone 60)
        StructureDataType slot = st("RenderStateSlot", 0x14, "One cached Direct3D render state.");
        f(slot, 0x00, PTR, "vtable", "+4 restore, +8 apply to Direct3D");
        f(slot, 0x04, PTR, "undoRecord", "current undo record");
        f(slot, 0x0C, PTR, "dirtyNode", "node in the dirty list, or NULL");
        f(slot, 0x10, UINT, "value", "cached value (or resource pointer)");
        DataType Slot = add(slot);

        StructureDataType cache = st("RenderStateCache", 0x5C, "Undo stack and dirty list behind RenderState_SetValue / PushScope / PopScope / Flush.");
        f(cache, 0x28, PTR, "dirtyHead", "slots changed since the last RenderState_Flush");
        f(cache, 0x2C, PTR, "dirtyTail", null);
        f(cache, 0x30, PTR, "nodePool", "Pool_Alloc / Pool_Free");
        f(cache, 0x38, PTR, "currentScope", "top undo scope record");
        f(cache, 0x50, PTR, "undoBase", null);
        f(cache, 0x54, PTR, "undoTop", "records grow downward, 0x14 bytes each");
        f(cache, 0x58, PTR, "undoPending", "records waiting for RenderState_CompactUndo");
        DataType Cache = add(cache);

        StructureDataType rend = st("Renderer", 0x64 + 198 * 0x14, "The renderer (static object 0x46B2E8, g_renderer).");
        f(rend, 0x00, BYTE, "active", null);
        f(rend, 0x04, PTR, "device", "Direct3D device");
        f(rend, 0x08, Cache, "stateCache", null);
        f(rend, 0x64, new ArrayDataType(Slot, 198, 0x14, dtm), "slots", "Render_SetStartupStates sets their defaults");
        DataType Renderer = add(rend);

        // ---- data-object base (Milestone 62)
        StructureDataType dax = st("DaxObject", 0x3C, "Base of the loadable data objects (DaxObject_ctor): IDatabaseEvent + IDAXProperty + IDAComponent.");
        f(dax, 0x00, PTR, "vtable", "IDatabaseEvent / IDAComponent");
        f(dax, 0x14, PTR, "vtableDAXProperty", "IDAXProperty interface");
        f(dax, 0x24, PTR, "currentReader", "top of the property-reader stack");
        f(dax, 0x2C, PTR, "readerStackBegin", "pairs of handlers, 8 bytes each");
        f(dax, 0x30, PTR, "readerStackEnd", null);
        f(dax, 0x34, PTR, "readerStackCapacity", null);
        f(dax, 0x38, INT, "refCount", "DaxObject_AddRef / DaxObject_Release");
        add(dax);

        // ---- Weapon (0x384, Weapon_ctor; an inventory item, itself a game object) - Milestone 67
        StructureDataType wp = st("Weapon", 0x384, "Weapon instance (Weapon_ctor, 0x384 bytes). Fields from the Weapon_* functions; inferred.");
        f(wp, 0x000, PTR, "vtable", "g_vt_Weapon");
        f(wp, 0x010, INT, "lastUpdateTick", "GameObject");
        f(wp, 0x048, PTR, "combatTargetVtable", "GameObject combat target sub-object");
        f(wp, 0x060, PTR, "shield", "GameObject");
        f(wp, 0x1F0, FLT, "maxDamage", "Weapon_RollDamage");
        f(wp, 0x1F4, FLT, "minDamage", "Weapon_RollDamage");
        f(wp, 0x1FC, PTR, "bulletData", "bullet-data record (Weapon_SpawnProjectile)");
        f(wp, 0x224, UINT, "triggerFlags", "bit 2: trigger held (Weapon_UpdateTrigger)");
        f(wp, 0x360, UINT, "projectileArchetype", "name hash of the BTYPE_MESH_PROJECTILE archetype");
        DataType Weapon = add(wp);

        StructureDataType pj = st("Projectile", 0x64, "Projectile from the projectile manager (world->projectiles). Size not known; fields from Projectile_* and Weapon_SpawnProjectile.");
        f(pj, 0x020, FLT, "baseDamage", "Projectile_GetDamage; reduced with distance when flag 0x40 is set");
        f(pj, 0x044, PTR, "shooter", "object that fired it");
        f(pj, 0x058, PTR, "hitMap", "targets already hit (Projectile_RecordHit)");
        DataType Projectile = add(pj);

        StructureDataType ai = st("AIController", 0x9E0, "AI controller (AIController_ctor, 0x9E0 bytes).");
        f(ai, 0x000, PTR, "vtable", "g_vt_AIController");
        f(ai, 0x924, FLT, "thinkTimer", "counted down by AIController_Think");
        DataType AIController = add(ai);

        // ---- globals
        int g = 0;
        g += setGlobal(0x4698E0L, ptr(Renderer));
        g += setGlobal(0x46999CL, ptr(World));
        println("Brute Force types v4: structures created; " + g + " global pointers typed.");

        // ---- object parameter of Character_*, World_*, Weapon_*, Projectile_*, AIController_* functions
        int typed = 0, skipped = 0;
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            monitor.checkCancelled();
            Function fn = it.next();
            String n = fn.getName();
            DataType target = null;
            String var = null;
            if (n.startsWith("Character_") && !n.equals("Character_ctor")) { target = ptr(Character); var = "character"; }
            else if (n.startsWith("World_") && !n.startsWith("World_RayCastObjects") && !n.equals("World_ctor")
                    && !n.equals("World_LineOfSight")) { target = ptr(World); var = "world"; }
            else if (n.startsWith("Weapon_") && !n.equals("Weapon_ctor")) { target = ptr(Weapon); var = "weapon"; }
            else if (n.startsWith("Projectile_")) { target = ptr(Projectile); var = "projectile"; }
            else if (n.startsWith("AIController_") && !n.equals("AIController_ctor")) { target = ptr(AIController); var = "ai"; }
            if (target == null) continue;
            if (setThis(fn, target, var)) typed++; else skipped++;
        }
        println("Brute Force types: object parameter typed on " + typed + " functions, " + skipped
            + " skipped (no object parameter, or it is a float).");
        if (di != null) di.dispose();
    }

    private int debug = 0;
    private DecompInterface di;
    private DecompInterface decomp() {
        if (di == null) {
            di = new DecompInterface();
            DecompileOptions o = new DecompileOptions();
            o.grabFromProgram(currentProgram);
            di.setOptions(o);
            di.openProgram(currentProgram);
        }
        return di;
    }

    private int setGlobal(long a, DataType t) {
        try {
            Address addr = toAddr(a);
            DataUtilities.createData(currentProgram, addr, t, -1, DataUtilities.ClearDataMode.CLEAR_ALL_CONFLICT_DATA);
            return 1;
        } catch (Exception e) {
            println("Could not type global 0x" + Long.toHexString(a) + ": " + e.getMessage());
            return 0;
        }
    }

    // Types the first parameter (the object: in this binary Ghidra shows it as param_1 for
    // __thiscall, __fastcall and plain functions alike) and names it after the class.
    private boolean setThis(Function fn, DataType t, String varName) {
        try {
            Parameter[] ps = fn.getParameters();
            if (ps.length == 0 && debug < 8) println("  " + fn.getName() + ": no stored parameters, committing the decompiler's");
            if (ps.length == 0) {
                // The parameters exist only in the decompiler's view: store them in the program first.
                DecompileResults r = decomp().decompileFunction(fn, 60, monitor);
                if (r == null || r.getHighFunction() == null) return false;
                HighFunctionDBUtil.commitParamsToDatabase(r.getHighFunction(), true,
                    HighFunctionDBUtil.ReturnCommitOption.NO_COMMIT, SourceType.ANALYSIS);
                ps = fn.getParameters();
                if (ps.length == 0) { if (debug++ < 8) println("  skip " + fn.getName() + ": still no parameters"); return false; }
            }
            Parameter p0 = ps[0];
            DataType cur = p0.getDataType();
            if (p0.getLength() != 4 || cur instanceof AbstractFloatDataType) {
                if (debug++ < 8) println("  skip " + fn.getName() + ": " + ps.length + " params, first " + cur.getName() + " (" + p0.getLength() + " bytes)");
                return false;
            }
            if (p0.isAutoParameter() || "__thiscall".equals(fn.getCallingConventionName())) {
                if (!fn.hasCustomVariableStorage()) fn.setCustomVariableStorage(true);
                p0 = fn.getParameter(0);
            }
            p0.setDataType(t, SourceType.USER_DEFINED);
            p0.setName(varName, SourceType.USER_DEFINED);
            return true;
        } catch (Exception e) {
            println("Could not type the object parameter of " + fn.getName() + ": " + e.getMessage());
        }
        return false;
    }
}
