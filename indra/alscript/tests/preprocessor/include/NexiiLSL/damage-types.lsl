#define DAMAGE_TYPE_MEDICAL 100
#define DAMAGE_TYPE_REPAIR 101
#define DAMAGE_TYPE_EXPLOSIVE 102
#define DAMAGE_TYPE_CRUSHING 103
#define DAMAGE_TYPE_ANTI_ARMOR 104
#define DAMAGE_TYPE_SUFFOCATION 105
#define DAMAGE_TYPE_REDEPLOY 106

// Returns name of the constant that identifies the damage type
string DamageTypeAsConstant(integer type)
{
    if(type == DAMAGE_TYPE_IMPACT) return "DAMAGE_TYPE_IMPACT";
    if(type == DAMAGE_TYPE_GENERIC) return "DAMAGE_TYPE_GENERIC";
    if(type == DAMAGE_TYPE_ACID) return "DAMAGE_TYPE_ACID";
    if(type == DAMAGE_TYPE_BLUDGEONING) return "DAMAGE_TYPE_BLUDGEONING";
    if(type == DAMAGE_TYPE_COLD) return "DAMAGE_TYPE_COLD";
    if(type == DAMAGE_TYPE_ELECTRIC) return "DAMAGE_TYPE_ELECTRIC";
    if(type == DAMAGE_TYPE_FIRE) return "DAMAGE_TYPE_FIRE";
    if(type == DAMAGE_TYPE_FORCE) return "DAMAGE_TYPE_FORCE";
    if(type == DAMAGE_TYPE_NECROTIC) return "DAMAGE_TYPE_NECROTIC";
    if(type == DAMAGE_TYPE_PIERCING) return "DAMAGE_TYPE_PIERCING";
    if(type == DAMAGE_TYPE_POISON) return "DAMAGE_TYPE_POISON";
    if(type == DAMAGE_TYPE_PSYCHIC) return "DAMAGE_TYPE_PSYCHIC";
    if(type == DAMAGE_TYPE_RADIANT) return "DAMAGE_TYPE_RADIANT";
    if(type == DAMAGE_TYPE_SLASHING) return "DAMAGE_TYPE_SLASHING";
    if(type == DAMAGE_TYPE_SONIC) return "DAMAGE_TYPE_SONIC";
    if(type == DAMAGE_TYPE_EMOTIONAL) return "DAMAGE_TYPE_EMOTIONAL";
    if(type == DAMAGE_TYPE_MEDICAL) return "DAMAGE_TYPE_MEDICAL";
    if(type == DAMAGE_TYPE_REPAIR) return "DAMAGE_TYPE_REPAIR";
    if(type == DAMAGE_TYPE_EXPLOSIVE) return "DAMAGE_TYPE_EXPLOSIVE";
    if(type == DAMAGE_TYPE_CRUSHING) return "DAMAGE_TYPE_CRUSHING";
    if(type == DAMAGE_TYPE_ANTI_ARMOR) return "DAMAGE_TYPE_ANTI_ARMOR";
    if(type == DAMAGE_TYPE_SUFFOCATION) return "DAMAGE_TYPE_SUFFOCATION";
    if(type == DAMAGE_TYPE_REDEPLOY) return "DAMAGE_TYPE_REDEPLOY";
    return (string)type;
}

// Returns damage type as a noun for use in sentences to describe the damage type
string DamageTypeAsNoun(integer type)
{
    if(type == DAMAGE_TYPE_IMPACT) return "impact";
    if(type == DAMAGE_TYPE_GENERIC) return "generic";
    if(type == DAMAGE_TYPE_ACID) return "acid";
    if(type == DAMAGE_TYPE_BLUDGEONING) return "bludgeoning";
    if(type == DAMAGE_TYPE_COLD) return "cold";
    if(type == DAMAGE_TYPE_ELECTRIC) return "electric";
    if(type == DAMAGE_TYPE_FIRE) return "fire";
    if(type == DAMAGE_TYPE_FORCE) return "force";
    if(type == DAMAGE_TYPE_NECROTIC) return "necrotic";
    if(type == DAMAGE_TYPE_PIERCING) return "shot"; // "piercing";
    if(type == DAMAGE_TYPE_POISON) return "poison";
    if(type == DAMAGE_TYPE_PSYCHIC) return "psychic";
    if(type == DAMAGE_TYPE_RADIANT) return "radiant";
    if(type == DAMAGE_TYPE_SLASHING) return "slashing";
    if(type == DAMAGE_TYPE_SONIC) return "sonic";
    if(type == DAMAGE_TYPE_EMOTIONAL) return "emotional";
    if(type == DAMAGE_TYPE_MEDICAL) return "medical";
    if(type == DAMAGE_TYPE_REPAIR) return "repair";
    if(type == DAMAGE_TYPE_EXPLOSIVE) return "explosive";
    if(type == DAMAGE_TYPE_CRUSHING) return "crushing";
    if(type == DAMAGE_TYPE_ANTI_ARMOR) return "anti-armor";
    if(type == DAMAGE_TYPE_SUFFOCATION) return "suffocation";
    if(type == DAMAGE_TYPE_REDEPLOY) return "redeploy";
    return "unknown";
}

// Returns damage type as a verb for use in sentences as how it was applied to a target
string DamageTypeAsVerb(integer type)
{
    if(type == DAMAGE_TYPE_IMPACT) return "impacted";
    if(type == DAMAGE_TYPE_GENERIC) return "damaged";
    if(type == DAMAGE_TYPE_ACID) return "corroded";
    if(type == DAMAGE_TYPE_BLUDGEONING) return "bludgeoned";
    if(type == DAMAGE_TYPE_COLD) return "frozen";
    if(type == DAMAGE_TYPE_ELECTRIC) return "electrocuted";
    if(type == DAMAGE_TYPE_FIRE) return "burnt";
    if(type == DAMAGE_TYPE_FORCE) return "smashed";
    if(type == DAMAGE_TYPE_NECROTIC) return "necrotised";
    if(type == DAMAGE_TYPE_PIERCING) return "shot"; // "pierced";
    if(type == DAMAGE_TYPE_POISON) return "poisoned";
    if(type == DAMAGE_TYPE_PSYCHIC) return "assaulted";
    if(type == DAMAGE_TYPE_RADIANT) return "raptured";
    if(type == DAMAGE_TYPE_SLASHING) return "slashed";
    if(type == DAMAGE_TYPE_SONIC) return "ruptured";
    if(type == DAMAGE_TYPE_EMOTIONAL) return "traumatised";
    if(type == DAMAGE_TYPE_MEDICAL) return "healed";
    if(type == DAMAGE_TYPE_REPAIR) return "repaired";
    if(type == DAMAGE_TYPE_EXPLOSIVE) return "exploded";
    if(type == DAMAGE_TYPE_CRUSHING) return "crushed";
    if(type == DAMAGE_TYPE_ANTI_ARMOR) return "punched through";
    if(type == DAMAGE_TYPE_SUFFOCATION) return "suffocated";
    if(type == DAMAGE_TYPE_REDEPLOY) return "redeployed";
    return "damaged";
}

// Returns PRIM_TEXTURE params for rendering the damage type as an icon
list DamageTypeAsIcon(integer type)
{
    vector offset; integer variant = (integer)llFrand(32.0);
    if(type == DAMAGE_TYPE_IMPACT) offset = <variant % 4, 0, 0>;
    else if(type == DAMAGE_TYPE_GENERIC) offset = <4, 0, 0>;
    else if(type == DAMAGE_TYPE_ACID)  offset = <5, 0, 0>;
    else if(type == DAMAGE_TYPE_BLUDGEONING)  offset = <6, 0, 0>;
    else if(type == DAMAGE_TYPE_COLD)  offset = <7, 0, 0>;
    else if(type == DAMAGE_TYPE_ELECTRIC)  offset = <8, 0, 0>;
    else if(type == DAMAGE_TYPE_FIRE)  offset = <9, 0, 0>;
    else if(type == DAMAGE_TYPE_FORCE)  offset = <10, 0, 0>;
    else if(type == DAMAGE_TYPE_NECROTIC)  offset = <11, 0, 0>;
    else if(type == DAMAGE_TYPE_PIERCING)  offset = <12, 0, 0>;
    else if(type == DAMAGE_TYPE_POISON)  offset = <13, 0, 0>;
    else if(type == DAMAGE_TYPE_PSYCHIC)  offset = <14, 0, 0>;
    else if(type == DAMAGE_TYPE_RADIANT)  offset = <15, 0, 0>;
    else if(type == DAMAGE_TYPE_SLASHING)  offset = <0, 1, 0>;
    else if(type == DAMAGE_TYPE_SONIC)  offset = <1 + (variant % 2), 1, 0>;
    else if(type == DAMAGE_TYPE_EMOTIONAL)  offset = <3, 1, 0>;
    else if(type == DAMAGE_TYPE_MEDICAL)  offset = <4 + (variant % 2), 1, 0>;
    else if(type == DAMAGE_TYPE_REPAIR)  offset = <6 + (variant % 2), 1, 0>;
    else if(type == DAMAGE_TYPE_EXPLOSIVE)  offset = <8, 1, 0>;
    else if(type == DAMAGE_TYPE_CRUSHING)  offset = <9, 1, 0>;
    else if(type == DAMAGE_TYPE_ANTI_ARMOR)  offset = <10, 1, 0>;
    else if(type == DAMAGE_TYPE_SUFFOCATION)  offset = <12, 1, 0>;
    else if(type == DAMAGE_TYPE_REDEPLOY) offset = <8, 3, 0>;
    
    // These are not damage types but additional icons related to combat
    else if(type == -100) offset = <0, 3, 0>; // Death
    else if(type == -101) offset = <1, 3, 0>; // Enemy Death
    else if(type == -102) offset = <2, 3, 0>; // Friendly Death
    else if(type == -103) offset = <3, 3, 0>; // Object Death
    else if(type == -104) offset = <4, 3, 0>; // Vehicle Death
    else if(type == -105) offset = <5, 3, 0>; // Aircraft Death
    else if(type == -106) offset = <6, 3, 0>; // Unconscious/Downed
    else if(type == -107) offset = <7, 3, 0>; // Revive Requested
    else if(type == -108) offset = <8, 3, 0>; // Being Revived
    
    /*
        Potential other icons:
        - Respawn, Redeploy
        - Mech Death
        - Shield Hit, Armor Hit, Critical Hit, EMP Hit
        - Critical Hit, Enemy Critical Hit, Friendly Critical Hit
        - Blocked Hit, Enemy Blocked Hit, Friendly Blocked Hit
        - Missed Hit, Enemy Missed Hit, Friendly Missed Hit
        - Civilian Down, Enemy Down, Friendly Down
        - Kill Assist
        - Headshot
        - Target Locked, Target Lost
        - Out of Ammo, Low Ammo, Reloading
        - Overheated, Jammed, Hacked, Disarmed, Suppressed, Spotted, Invisible
        - Invulnerable, Vulnerable
        - Buffed, Debuffed
    */
    
    return [
        "2d053029-7baa-104d-fd8e-34b2112b4b78",
        <128./2048., 128./512., 0>,
        <(offset.x - 7.5) / 16, (1.5 - offset.y) / 4, 0>,
        0
    ];
}

string DamageTypeAsReason(integer type, key owner, key target)
{
    // Picks a uniformly random entry from a list. Avoids the modulo bias
    // of llFrand(64.0) % N and means you never have to update counts when
    // adding/removing lines.
    #define PICK(l) llList2String(l, (integer)llFrand((float)llGetListLength(l)))

    // ============================================================
    // ATTACKER vs VICTIM (owner != target)
    // ============================================================
    #define FRIENDLY_FIRE [\
        "OWNER confirmed TARGET as 'probably hostile'",\
        "OWNER eliminated TARGET with outstanding target identification skills",\
        "OWNER won the team's internal conflict against TARGET",\
        "OWNER revealed to TARGET that the IFF system was decorative",\
        "OWNER engaged the nearest available lifeform: TARGET",\
        "OWNER delivered allied fire to TARGET. Emphasis on allied",\
        "OWNER mistook TARGET for a tactical opportunity",\
        "OWNER killed TARGET with an aggressive interpretation of teamwork",\
        "OWNER secured a friendly kill on TARGET. Mission unclear",\
        "OWNER taught TARGET not to stand between a squadmate and bad decisions",\
        "OWNER demonstrated why positive identification matters. TARGET objected briefly",\
        "OWNER greeted TARGET with extremely friendly burst fire",\
        "OWNER's suppressive fire caught TARGET non-suppressed",\
        "OWNER showed TARGET a safety selector permanently set to optimism",\
        "OWNER found TARGET faster than the enemy did",\
        "OWNER introduced TARGET to friendly fire",\
        "OWNER secured an enemy-adjacent kill on TARGET",\
        "OWNER promoted TARGET to hostile",\
        "OWNER carried the enemy team by killing TARGET",\
        "OWNER won an internal faction dispute against TARGET",\
        "OWNER treated TARGET like hostile armor",\
        "OWNER killed TARGET with unwavering confidence and limited accuracy",\
        "OWNER's line of fire met TARGET head-on",\
        "OWNER killed TARGET after TARGET challenged basic firearm safety",\
        "OWNER defended their muzzle space from TARGET",\
        "OWNER shot TARGET, who appeared suddenly and unhelpfully",\
        "OWNER's burst fire awarded TARGET a prize for crossing it",\
        "OWNER eliminated TARGET, who selected poor timing as a lifestyle",\
        "OWNER hit TARGET with rounds clearly intended for somebody else",\
        "OWNER's bullets met TARGET halfway",\
        "OWNER solved a tactical problem that TARGET inserted themselves into",\
        "OWNER killed TARGET after TARGET performed unauthorized bullet interception",\
        "OWNER was already shooting exactly where TARGET peeked",\
        "OWNER dropped TARGET, who entered the doorway at maximum inconvenience",\
        "OWNER engaged the enemy while TARGET was hugging it",\
        "OWNER's suppressive fire was suppressed by TARGET's positioning",\
        "OWNER demonstrated why you don't cross the firing lane. TARGET volunteered",\
        "OWNER secured a friendly kill after TARGET sprinted through active gunfire",\
        "OWNER penalized TARGET for performing close-quarters teamwork incorrectly",\
        "OWNER shot TARGET, who materialized directly in the optic picture",\
        "OWNER failed a trigger discipline test administered by TARGET at point-blank range",\
        "OWNER accidentally fulfilled TARGET's request for covering fire",\
        "OWNER charged TARGET full price for using the firing lane as a shortcut",\
        "OWNER killed TARGET, who confused suppressive fire with navigational guidance",\
        "OWNER confirmed TARGET located the line of fire by standing in it",\
        "OWNER eliminated TARGET after TARGET ignored several visible warning signs",\
        "OWNER intervened in TARGET's attempt at advanced teamwork without situational awareness",\
        "OWNER shot TARGET, who committed aggressively to being in the way",\
        "OWNER's ballistic calculations gained an unexpected variable: TARGET",\
        "OWNER's aim was fine. TARGET's pathfinding was not",\
        "OWNER cleared TARGET out from between themselves and success",\
        "OWNER killed TARGET after TARGET made themselves tactically unavoidable"\
    ]
    #define IMPACT [\
        "OWNER knocked TARGET into a wall",\
        "OWNER slammed TARGET into cover",\
        "OWNER sent TARGET crashing into terrain",\
        "OWNER introduced TARGET to the ground at speed",\
        "OWNER forced TARGET into a fatal collision",\
        "OWNER's fire cancelled TARGET's momentum permanently",\
        "OWNER knocked TARGET off balance and into solid concrete",\
        "OWNER pressured TARGET into pancaking against the environment",\
        "OWNER turned gravity against TARGET",\
        "OWNER arranged a high-speed argument between TARGET and physics. Physics won"\
    ]
    #define GENERIC [\
        "OWNER killed TARGET",\
        "OWNER eliminated TARGET",\
        "OWNER took down TARGET",\
        "OWNER neutralized TARGET",\
        "OWNER removed TARGET from the battlefield",\
        "OWNER put TARGET down",\
        "OWNER dropped TARGET",\
        "OWNER defeated TARGET",\
        "OWNER finished TARGET",\
        "OWNER took TARGET out of the fight"\
    ]
    #define ACID [\
        "OWNER dissolved TARGET with corrosives",\
        "OWNER melted TARGET with a chemical attack",\
        "OWNER splashed TARGET with lethal acid",\
        "OWNER delivered a corrosive payload directly to TARGET",\
        "OWNER reduced TARGET's gear to sludge",\
        "OWNER chemically erased TARGET",\
        "OWNER turned chemistry into a weapon against TARGET",\
        "OWNER's acid attack proved too much for TARGET",\
        "OWNER corroded TARGET beyond repair",\
        "OWNER liquefied TARGET"\
    ]
    #define BLUDGEONING [\
        "OWNER beat TARGET down",\
        "OWNER crushed TARGET with blunt force",\
        "OWNER smashed TARGET into submission",\
        "OWNER landed a fatal blunt hit on TARGET",\
        "OWNER battered TARGET into the dirt",\
        "OWNER flattened TARGET",\
        "OWNER hammered TARGET",\
        "OWNER's heavy impact ended TARGET",\
        "OWNER delivered overwhelming blunt trauma to TARGET",\
        "OWNER broke TARGET with a single strike"\
    ]
    #define COLD [\
        "OWNER froze TARGET solid",\
        "OWNER's cryogenic attack claimed TARGET",\
        "OWNER chilled TARGET beyond recovery",\
        "OWNER iced TARGET over",\
        "OWNER turned TARGET into a frozen casualty",\
        "OWNER subjected TARGET to lethal cold exposure",\
        "OWNER locked TARGET down with extreme cold",\
        "OWNER left TARGET frozen mid-fight",\
        "OWNER weaponized winter against TARGET",\
        "OWNER won the heat war against TARGET"\
    ]
    #define ELECTRIC [\
        "OWNER electrocuted TARGET",\
        "OWNER shocked TARGET to death",\
        "OWNER overloaded TARGET with electricity",\
        "OWNER delivered lethal voltage to TARGET",\
        "OWNER lit TARGET up with electrical discharge",\
        "OWNER shut down TARGET's nervous system",\
        "OWNER short-circuited TARGET",\
        "OWNER applied a high-voltage solution to TARGET",\
        "OWNER fried TARGET",\
        "OWNER made TARGET spark out"\
    ]
    #define FIRE [\
        "OWNER burned TARGET alive",\
        "OWNER incinerated TARGET",\
        "OWNER engulfed TARGET in flames",\
        "OWNER cooked TARGET with incendiaries",\
        "OWNER reduced TARGET to ashes",\
        "OWNER set TARGET on fire. Permanently",\
        "OWNER scorched TARGET off the battlefield",\
        "OWNER won the thermal exchange with TARGET",\
        "OWNER roasted TARGET",\
        "OWNER's flames consumed TARGET"\
    ]
    #define FORCE [\
        "OWNER blasted TARGET apart",\
        "OWNER launched TARGET",\
        "OWNER overwhelmed TARGET with raw force",\
        "OWNER hit TARGET with a fatal shockwave",\
        "OWNER hit TARGET with devastating force",\
        "OWNER's impact proved unsurvivable for TARGET",\
        "OWNER violently displaced TARGET",\
        "OWNER freight-trained TARGET",\
        "OWNER crushed TARGET with concussive power",\
        "OWNER blew TARGET clean off their position"\
    ]
    #define NECROTIC [\
        "OWNER drained the life from TARGET",\
        "OWNER withered TARGET away",\
        "OWNER consumed TARGET's vitality",\
        "OWNER accelerated TARGET's decay considerably",\
        "OWNER left TARGET biologically ruined",\
        "OWNER's lethal touch faded TARGET out",\
        "OWNER reduced TARGET to a husk",\
        "OWNER outlasted TARGET's life force",\
        "OWNER brought slow death to TARGET",\
        "OWNER's necrotic damage claimed TARGET"\
    ]
    #define PIERCING [\
        "OWNER riddled TARGET with bullets",\
        "OWNER cut TARGET down with rifle fire",\
        "OWNER stitched TARGET with automatic fire",\
        "OWNER put a burst into TARGET",\
        "OWNER perforated TARGET with incoming rounds",\
        "OWNER drilled TARGET with gunfire",\
        "OWNER landed center-mass hits on TARGET",\
        "OWNER won the firefight against TARGET",\
        "OWNER filled TARGET with lead",\
        "OWNER ventilated TARGET"\
    ]
    #define POISON [\
        "OWNER poisoned TARGET",\
        "OWNER's toxins claimed TARGET",\
        "OWNER contaminated TARGET fatally",\
        "OWNER chemically deteriorated TARGET",\
        "OWNER dosed TARGET with lethal poison",\
        "OWNER convinced TARGET's body to give up",\
        "OWNER weaponized toxins against TARGET",\
        "OWNER's poison ran its full course on TARGET",\
        "OWNER delivered a toxic end to TARGET",\
        "OWNER gave TARGET a fatal chemical exposure"\
    ]
    #define PSYCHIC [\
        "OWNER shattered TARGET's mind",\
        "OWNER caused TARGET's complete mental collapse",\
        "OWNER overwhelmed TARGET psychologically",\
        "OWNER out-thought TARGET fatally",\
        "OWNER broke TARGET mentally",\
        "OWNER won the cognitive battle against TARGET",\
        "OWNER overloaded TARGET's mind",\
        "OWNER psychologically neutralized TARGET",\
        "OWNER destabilized TARGET completely",\
        "OWNER gave TARGET more than TARGET could process"\
    ]
    #define RADIANT [\
        "OWNER overwhelmed TARGET with radiant energy",\
        "OWNER burned TARGET with radiation",\
        "OWNER blasted TARGET with destructive light",\
        "OWNER made TARGET glow. Briefly",\
        "OWNER irradiated TARGET fatally",\
        "OWNER's energy discharge claimed TARGET",\
        "OWNER seared TARGET with pure energy",\
        "OWNER outshone TARGET fatally",\
        "OWNER exposed TARGET to lethal radiation",\
        "OWNER annihilated TARGET with raw energy"\
    ]
    #define SLASHING [\
        "OWNER cut TARGET down",\
        "OWNER carved TARGET apart",\
        "OWNER opened TARGET up",\
        "OWNER's blade met TARGET. Once was enough",\
        "OWNER sliced through TARGET",\
        "OWNER left TARGET with fatal cuts",\
        "OWNER shredded TARGET",\
        "OWNER tore TARGET apart",\
        "OWNER made short work of TARGET",\
        "OWNER showed TARGET the sharp end"\
    ]
    #define SONIC [\
        "OWNER blasted TARGET with sonic force",\
        "OWNER collapsed TARGET's senses",\
        "OWNER overwhelmed TARGET with pressure waves",\
        "OWNER's sonic blast proved fatal for TARGET",\
        "OWNER deafened TARGET permanently",\
        "OWNER rocked TARGET with acoustic weaponry",\
        "OWNER weaponized sound against TARGET",\
        "OWNER won the volume war against TARGET",\
        "OWNER shattered TARGET with sonic energy",\
        "OWNER concussed TARGET with a sonic assault"\
    ]
    #define EMOTIONAL [\
        "OWNER emotionally devastated TARGET",\
        "OWNER collapsed TARGET's morale entirely",\
        "OWNER broke TARGET's spirit",\
        "OWNER dealt critical emotional damage to TARGET",\
        "OWNER crushed TARGET's confidence",\
        "OWNER won the psychological war against TARGET",\
        "OWNER destroyed TARGET's morale",\
        "OWNER applied pressure TARGET never recovered from",\
        "OWNER left TARGET mentally defeated",\
        "OWNER made sure TARGET took the loss personally"\
    ]
    #define MEDICAL [\
        "OWNER's treatment killed TARGET",\
        "OWNER's medical intervention proved fatal to TARGET",\
        "OWNER flatlined TARGET during treatment",\
        "OWNER gave TARGET one treatment too many",\
        "OWNER's battlefield medicine failed TARGET catastrophically",\
        "OWNER's bedside manner killed TARGET",\
        "OWNER healed TARGET into the afterlife",\
        "OWNER's medkit betrayed TARGET's trust",\
        "OWNER's treatment proved terminal for TARGET",\
        "OWNER medically neutralized TARGET"\
    ]
    #define REPAIR [\
        "OWNER repaired TARGET into catastrophic failure",\
        "OWNER's maintenance proved unsurvivable for TARGET",\
        "OWNER's field repairs ended TARGET",\
        "OWNER performed rapid unscheduled disassembly on TARGET",\
        "OWNER pushed TARGET beyond operational limits",\
        "OWNER's repair attempt finished TARGET off",\
        "OWNER turned maintenance into a kill",\
        "OWNER's adjustments collapsed TARGET's systems",\
        "OWNER overclocked TARGET to destruction",\
        "OWNER rendered TARGET permanently nonfunctional"\
    ]
    #define EXPLOSIVE [\
        "OWNER blew TARGET apart",\
        "OWNER caught TARGET in the blast radius",\
        "OWNER introduced TARGET to high explosives",\
        "OWNER fed TARGET a grenade",\
        "OWNER detonated TARGET",\
        "OWNER's explosives outran TARGET",\
        "OWNER erased TARGET with a blast",\
        "OWNER's detonation shredded TARGET",\
        "OWNER reduced TARGET to debris",\
        "OWNER's ordnance found TARGET standing too close"\
    ]
    #define CRUSHING [\
        "OWNER crushed TARGET",\
        "OWNER flattened TARGET completely",\
        "OWNER compressed TARGET into failure",\
        "OWNER pinned and crushed TARGET",\
        "OWNER turned pressure into a weapon against TARGET",\
        "OWNER folded TARGET like field equipment",\
        "OWNER reduced TARGET to scrap",\
        "OWNER compacted TARGET",\
        "OWNER buried TARGET under overwhelming pressure",\
        "OWNER ran a compression test on TARGET. TARGET failed"\
    ]
    #define ANTI_ARMOR [\
        "OWNER punched through TARGET's armor",\
        "OWNER penetrated TARGET with anti-armor fire",\
        "OWNER defeated TARGET with AP rounds",\
        "OWNER proved TARGET's armor was a suggestion",\
        "OWNER cracked TARGET open with anti-vehicle fire",\
        "OWNER's penetrators went straight through TARGET",\
        "OWNER turned TARGET's armor into a liability",\
        "OWNER gutted TARGET with heavy ordnance",\
        "OWNER landed a clean anti-armor kill on TARGET",\
        "OWNER won the armor check against TARGET"\
    ]
    #define SUFFOCATION [\
        "OWNER deprived TARGET of oxygen",\
        "OWNER suffocated TARGET",\
        "OWNER left TARGET without breathable air",\
        "OWNER ran TARGET out of air",\
        "OWNER choked the life out of TARGET",\
        "OWNER removed breathing from TARGET's options",\
        "OWNER turned atmosphere into a privilege TARGET didn't have",\
        "OWNER revoked TARGET's oxygen access",\
        "OWNER asphyxiated TARGET",\
        "OWNER reminded TARGET that breathing was mandatory. Too late"\
    ]

    // ============================================================
    // SELF-INFLICTED (owner == target) — TARGET-only phrasing
    // ============================================================
    #define IMPACT_SELF [\
        "TARGET punched the ground. The ground punched back",\
        "TARGET tested the structural integrity of a nearby wall",\
        "TARGET tested the structural integrity of a nearby floor",\
        "TARGET tested the structural integrity of a nearby ceiling",\
        "TARGET discovered the upper limit of their movement enhancer",\
        "TARGET achieved terminal mobility",\
        "TARGET mistook momentum for a survival strategy",\
        "TARGET lost a close-quarters engagement with the terrain",\
        "TARGET deployed directly into solid matter",\
        "TARGET accelerated confidently into disaster",\
        "TARGET's movement tech delivered exactly as advertised",\
        "TARGET learned that velocity has consequences",\
        "TARGET attempted advanced maneuvering without adult supervision",\
        "TARGET converted speed into an immediate medical issue",\
        "TARGET overshot the objective by several meters and one life",\
        "TARGET used tactical sprint offensively against themselves",\
        "TARGET was outplayed by basic map geometry",\
        "TARGET trusted the jump pack. The jump pack disagreed",\
        "TARGET discovered an undocumented interaction between boosters and concrete",\
        "TARGET briefly achieved flight before remembering they weren't aviation-capable",\
        "TARGET's landing request was denied by physics",\
        "TARGET found the map boundary emotionally and physically",\
        "TARGET turned mobility equipment into a kinetic weapon against TARGET",\
        "TARGET's combat stim demanded speed; the wall accepted payment",\
        "TARGET exceeded recommended operating velocity",\
        "TARGET weaponized their own momentum",\
        "TARGET slammed into cover with unnecessary enthusiasm",\
        "TARGET's parkour certification has been revoked",\
        "TARGET hit mach jesus and forgot the braking phase",\
        "TARGET went from combatant to projectile",\
        "TARGET speedran the respawn screen",\
        "TARGET demonstrated why boosters come with warning labels",\
        "TARGET challenged Newtonian mechanics and lost unanimously",\
        "TARGET's movement enhancer secured a confirmed kill",\
        "TARGET achieved excellent airtime and poor decision-making",\
        "TARGET discovered a new route directly into the ground",\
        "TARGET attempted vertical gameplay and received horizontal consequences",\
        "TARGET became one with nearby infrastructure"\
    ]
    #define GENERIC_SELF [\
        "TARGET eliminated TARGET. Efficient",\
        "TARGET cut out the middleman and killed themselves",\
        "TARGET found the enemy. It was TARGET",\
        "TARGET secured a confirmed kill on TARGET",\
        "TARGET defeated their greatest opponent: TARGET",\
        "TARGET needed no assistance dying",\
        "TARGET took themselves out of the fight",\
        "TARGET saved the enemy some ammunition"\
    ]
    #define ACID_SELF [\
        "TARGET dissolved in their own corrosives",\
        "TARGET learned that acid doesn't check IFF",\
        "TARGET became the test subject of their own chemistry experiment",\
        "TARGET stored the acid incorrectly: on themselves",\
        "TARGET handled corrosives with confidence instead of gloves",\
        "TARGET's chemical weapon chose violence locally",\
        "TARGET self-liquefied",\
        "TARGET reduced their own gear, and TARGET, to sludge"\
    ]
    #define BLUDGEONING_SELF [\
        "TARGET beat themselves to the punch. Fatally",\
        "TARGET delivered blunt trauma to the nearest available skull: their own",\
        "TARGET lost a fistfight against their own equipment",\
        "TARGET swung hard and connected with TARGET",\
        "TARGET self-administered percussive maintenance",\
        "TARGET was somehow on both ends of the hammer",\
        "TARGET flattened the closest combatant: TARGET",\
        "TARGET hammered themselves into the dirt unassisted"\
    ]
    #define COLD_SELF [\
        "TARGET froze themselves solid",\
        "TARGET stress-tested their own cryogenics from the inside",\
        "TARGET achieved self-refrigeration",\
        "TARGET forgot which end of the cryo weapon was the cold one",\
        "TARGET preserved themselves for future generations",\
        "TARGET lost the heat war against TARGET",\
        "TARGET turned themselves into a frozen casualty",\
        "TARGET weaponized winter against the only person in range: TARGET"\
    ]
    #define ELECTRIC_SELF [\
        "TARGET electrocuted themselves",\
        "TARGET completed the circuit personally",\
        "TARGET discovered they were the path of least resistance",\
        "TARGET grounded themselves. Permanently",\
        "TARGET conducted a one-person electrical safety demonstration",\
        "TARGET's high-voltage solution solved TARGET",\
        "TARGET short-circuited their own nervous system",\
        "TARGET fried the nearest conductor: TARGET"\
    ]
    #define FIRE_SELF [\
        "TARGET self-ignited",\
        "TARGET stood in their own fire and called it warmth",\
        "TARGET deployed incendiaries at extremely personal range",\
        "TARGET became their own thermal signature",\
        "TARGET played with fire and lost custody",\
        "TARGET achieved self-cremation ahead of schedule",\
        "TARGET lost the thermal exchange with TARGET",\
        "TARGET's flames showed no brand loyalty"\
    ]
    #define FORCE_SELF [\
        "TARGET blasted themselves apart",\
        "TARGET stood at the business end of their own shockwave",\
        "TARGET applied devastating force in the wrong direction",\
        "TARGET freight-trained TARGET",\
        "TARGET violently displaced themselves",\
        "TARGET's raw power needed a target. TARGET volunteered",\
        "TARGET blew themselves clean off their own position",\
        "TARGET found their own impact unsurvivable"\
    ]
    #define NECROTIC_SELF [\
        "TARGET drained their own life force",\
        "TARGET accelerated their own decay considerably",\
        "TARGET reduced themselves to a husk",\
        "TARGET's life force resigned without notice",\
        "TARGET consumed their own vitality. Bold strategy",\
        "TARGET brought slow death to the nearest lifeform: TARGET",\
        "TARGET faded themselves out",\
        "TARGET left TARGET biologically ruined"\
    ]
    #define PIERCING_SELF [\
        "TARGET shot themselves",\
        "TARGET checked if the gun was loaded the hard way",\
        "TARGET caught a burst from a familiar rifle: their own",\
        "TARGET lost a firefight with zero other participants",\
        "TARGET demonstrated muzzle awareness by counterexample",\
        "TARGET filled themselves with lead",\
        "TARGET ventilated TARGET",\
        "TARGET landed center-mass hits on the wrong center mass"\
    ]
    #define POISON_SELF [\
        "TARGET poisoned themselves",\
        "TARGET taste-tested their own toxins",\
        "TARGET confused the antidote with the dosage",\
        "TARGET's chemical weapon worked exactly once, on TARGET",\
        "TARGET self-administered a lethal dose",\
        "TARGET trusted their own labeling system incorrectly",\
        "TARGET's body gave up after consulting with TARGET",\
        "TARGET delivered a toxic end to TARGET"\
    ]
    #define PSYCHIC_SELF [\
        "TARGET thought themselves to death",\
        "TARGET lost an argument inside their own head",\
        "TARGET's mind overloaded itself",\
        "TARGET overthought a survivable situation. Fatally",\
        "TARGET was psychologically neutralized by TARGET",\
        "TARGET won the cognitive battle and also lost it",\
        "TARGET shattered their own mind unassisted",\
        "TARGET gave TARGET more than TARGET could process"\
    ]
    #define RADIANT_SELF [\
        "TARGET irradiated themselves",\
        "TARGET stood inside their own energy discharge",\
        "TARGET glowed with self-confidence. Then just glowed",\
        "TARGET ignored the safe operating distance of their own weapon",\
        "TARGET achieved self-illumination. Briefly",\
        "TARGET exposed themselves to lethal radiation. Their own",\
        "TARGET outshone TARGET fatally",\
        "TARGET annihilated the closest energy signature: TARGET"\
    ]
    #define SLASHING_SELF [\
        "TARGET cut themselves down",\
        "TARGET lost a knife fight against TARGET",\
        "TARGET found the sharp end of their own blade",\
        "TARGET demonstrated blade safety by counterexample",\
        "TARGET sliced through the nearest combatant: TARGET",\
        "TARGET made short work of themselves",\
        "TARGET opened TARGET up. No assistance required",\
        "TARGET's blade showed no loyalty"\
    ]
    #define SONIC_SELF [\
        "TARGET deafened themselves to death",\
        "TARGET stood inside their own pressure wave",\
        "TARGET lost the volume war against TARGET",\
        "TARGET turned the acoustic weapon up to eleven while holding it",\
        "TARGET concussed themselves with their own bass drop",\
        "TARGET's sonic blast didn't discriminate",\
        "TARGET collapsed their own senses",\
        "TARGET weaponized sound against the closest pair of ears: their own"\
    ]
    #define EMOTIONAL_SELF [\
        "TARGET's morale collapsed under TARGET",\
        "TARGET lost the psychological war with themselves",\
        "TARGET took the loss personally. From themselves",\
        "TARGET crushed their own confidence fatally",\
        "TARGET broke their own spirit before the enemy could",\
        "TARGET was their own harshest critic. Lethally",\
        "TARGET applied pressure TARGET never recovered from",\
        "TARGET emotionally devastated TARGET"\
    ]
    #define MEDICAL_SELF [\
        "TARGET self-medicated into the afterlife",\
        "TARGET's self-treatment proved terminal",\
        "TARGET read the medkit instructions posthumously",\
        "TARGET healed themselves to death",\
        "TARGET trusted their own medical credentials incorrectly",\
        "TARGET flatlined under their own care",\
        "TARGET gave TARGET one treatment too many",\
        "TARGET's bedside manner killed the only patient available: TARGET"\
    ]
    #define REPAIR_SELF [\
        "TARGET repaired themselves into catastrophic failure",\
        "TARGET performed rapid unscheduled self-disassembly",\
        "TARGET overclocked themselves to destruction",\
        "TARGET voided their own warranty",\
        "TARGET's self-maintenance proved unsurvivable",\
        "TARGET pushed TARGET beyond operational limits",\
        "TARGET turned self-maintenance into a confirmed kill",\
        "TARGET rendered TARGET permanently nonfunctional"\
    ]
    #define EXPLOSIVE_SELF [\
        "TARGET held the grenade slightly too long",\
        "TARGET stood in their own blast radius",\
        "TARGET cooked a grenade past the recommended doneness",\
        "TARGET became the epicenter",\
        "TARGET's ordnance arrived before TARGET left",\
        "TARGET reduced themselves to debris",\
        "TARGET couldn't outrun their own explosives",\
        "TARGET ate their own grenade. Chef's kiss"\
    ]
    #define CRUSHING_SELF [\
        "TARGET crushed themselves",\
        "TARGET was flattened by their own equipment",\
        "TARGET ran a compression test on TARGET. Both sides failed",\
        "TARGET parked something heavy on top of themselves",\
        "TARGET folded under pressure of their own making",\
        "TARGET compacted themselves for easy storage",\
        "TARGET reduced TARGET to scrap",\
        "TARGET turned pressure into a weapon against the only one in range: TARGET"\
    ]
    #define ANTI_ARMOR_SELF [\
        "TARGET penetrated their own armor",\
        "TARGET proved their own armor was a suggestion",\
        "TARGET tested AP rounds on the nearest armor: their own",\
        "TARGET turned their armor into a liability personally",\
        "TARGET cracked themselves open with anti-vehicle fire",\
        "TARGET won and lost the armor check simultaneously",\
        "TARGET's penetrators went straight through TARGET",\
        "TARGET gutted TARGET with heavy ordnance. Impressive angle"\
    ]
    #define SUFFOCATION_SELF [\
        "TARGET forgot breathing was mandatory",\
        "TARGET ran out of air with no help from anyone",\
        "TARGET revoked their own oxygen access",\
        "TARGET held their breath competitively against themselves",\
        "TARGET treated oxygen as optional",\
        "TARGET asphyxiated unassisted",\
        "TARGET removed breathing from their own options",\
        "TARGET turned atmosphere into a privilege TARGET didn't have"\
    ]
    #define REDEPLOY [\
        "TARGET redeployed to a new position",\
        "TARGET returned to the deployment queue",\
        "TARGET chose to redeploy",\
        "TARGET cycled back to spawn",\
        "TARGET withdrew for redeployment",\
        "TARGET repositioned via redeploy",\
        "TARGET abandoned the current position",\
        "TARGET respawned elsewhere",\
        "TARGET reset their tactical situation",\
        "TARGET redeployed out of the combat zone",\
        "TARGET took the express route back to spawn",\
        "TARGET performed a tactical respawn",\
        "TARGET sought better life choices via redeploy",\
        "TARGET changed their mind — and location",\
        "TARGET performed tactical disappearance",\
        "TARGET reconsidered their surroundings",\
        "TARGET pressed the redeploy button",\
        "TARGET found a more survivable ZIP code",\
        "TARGET left to spawn somewhere less explosive",\
        "TARGET took a strategic timeout"\
    ]

    string reason;

    // Redeploy is always a voluntary self-action, regardless of who is credited
    if(type == DAMAGE_TYPE_REDEPLOY)
    {
        reason = PICK(REDEPLOY);
    }
    else if(owner == target)
    {
        // Self-inflicted: TARGET-only phrasing, no attacker credit
        if(type == DAMAGE_TYPE_IMPACT) reason = PICK(IMPACT_SELF);
        else if(type == DAMAGE_TYPE_GENERIC) reason = PICK(GENERIC_SELF);
        else if(type == DAMAGE_TYPE_ACID) reason = PICK(ACID_SELF);
        else if(type == DAMAGE_TYPE_BLUDGEONING) reason = PICK(BLUDGEONING_SELF);
        else if(type == DAMAGE_TYPE_COLD) reason = PICK(COLD_SELF);
        else if(type == DAMAGE_TYPE_ELECTRIC) reason = PICK(ELECTRIC_SELF);
        else if(type == DAMAGE_TYPE_FIRE) reason = PICK(FIRE_SELF);
        else if(type == DAMAGE_TYPE_FORCE) reason = PICK(FORCE_SELF);
        else if(type == DAMAGE_TYPE_NECROTIC) reason = PICK(NECROTIC_SELF);
        else if(type == DAMAGE_TYPE_PIERCING) reason = PICK(PIERCING_SELF);
        else if(type == DAMAGE_TYPE_POISON) reason = PICK(POISON_SELF);
        else if(type == DAMAGE_TYPE_PSYCHIC) reason = PICK(PSYCHIC_SELF);
        else if(type == DAMAGE_TYPE_RADIANT) reason = PICK(RADIANT_SELF);
        else if(type == DAMAGE_TYPE_SLASHING) reason = PICK(SLASHING_SELF);
        else if(type == DAMAGE_TYPE_SONIC) reason = PICK(SONIC_SELF);
        else if(type == DAMAGE_TYPE_EMOTIONAL) reason = PICK(EMOTIONAL_SELF);
        else if(type == DAMAGE_TYPE_MEDICAL) reason = PICK(MEDICAL_SELF);
        else if(type == DAMAGE_TYPE_REPAIR) reason = PICK(REPAIR_SELF);
        else if(type == DAMAGE_TYPE_EXPLOSIVE) reason = PICK(EXPLOSIVE_SELF);
        else if(type == DAMAGE_TYPE_CRUSHING) reason = PICK(CRUSHING_SELF);
        else if(type == DAMAGE_TYPE_ANTI_ARMOR) reason = PICK(ANTI_ARMOR_SELF);
        else if(type == DAMAGE_TYPE_SUFFOCATION) reason = PICK(SUFFOCATION_SELF);
        else if(type == -100) reason = "TARGET team-killed the only teammate within reach: TARGET";
        else reason = "TARGET was killed by TARGET's own " + DamageTypeAsNoun(type) + " damage. Impressive";
    }
    else
    {
        // Attacker vs victim: OWNER-first phrasing
        if(type == DAMAGE_TYPE_IMPACT) reason = PICK(IMPACT);
        else if(type == DAMAGE_TYPE_GENERIC) reason = PICK(GENERIC);
        else if(type == DAMAGE_TYPE_ACID) reason = PICK(ACID);
        else if(type == DAMAGE_TYPE_BLUDGEONING) reason = PICK(BLUDGEONING);
        else if(type == DAMAGE_TYPE_COLD) reason = PICK(COLD);
        else if(type == DAMAGE_TYPE_ELECTRIC) reason = PICK(ELECTRIC);
        else if(type == DAMAGE_TYPE_FIRE) reason = PICK(FIRE);
        else if(type == DAMAGE_TYPE_FORCE) reason = PICK(FORCE);
        else if(type == DAMAGE_TYPE_NECROTIC) reason = PICK(NECROTIC);
        else if(type == DAMAGE_TYPE_PIERCING) reason = PICK(PIERCING);
        else if(type == DAMAGE_TYPE_POISON) reason = PICK(POISON);
        else if(type == DAMAGE_TYPE_PSYCHIC) reason = PICK(PSYCHIC);
        else if(type == DAMAGE_TYPE_RADIANT) reason = PICK(RADIANT);
        else if(type == DAMAGE_TYPE_SLASHING) reason = PICK(SLASHING);
        else if(type == DAMAGE_TYPE_SONIC) reason = PICK(SONIC);
        else if(type == DAMAGE_TYPE_EMOTIONAL) reason = PICK(EMOTIONAL);
        else if(type == DAMAGE_TYPE_MEDICAL) reason = PICK(MEDICAL);
        else if(type == DAMAGE_TYPE_REPAIR) reason = PICK(REPAIR);
        else if(type == DAMAGE_TYPE_EXPLOSIVE) reason = PICK(EXPLOSIVE);
        else if(type == DAMAGE_TYPE_CRUSHING) reason = PICK(CRUSHING);
        else if(type == DAMAGE_TYPE_ANTI_ARMOR) reason = PICK(ANTI_ARMOR);
        else if(type == -100) reason = PICK(FRIENDLY_FIRE);
        else if(type == DAMAGE_TYPE_SUFFOCATION) reason = PICK(SUFFOCATION);
        else reason = "OWNER killed TARGET with " + DamageTypeAsNoun(type) + " damage";
    }

    reason = llReplaceSubString(reason, "TARGET", "secondlife:///app/agent/" + (string)target + "/inspect", 0);
    reason = llReplaceSubString(reason, "OWNER", "secondlife:///app/agent/" + (string)owner + "/inspect", 0);
    return reason;
}


// Applies anti-armor damage in Combat2, with compatibility fallback to LBA (Listen Based Armor)
DamageArmor(key target, float damage)
{
    // Has Combat2 health, apply ANTI_ARMOR type
    if(llGetHealth(target) > 0)
    {
        llDamage(target, damage, DAMAGE_TYPE_ANTI_ARMOR);
        // llOwnerSay("Applied " + (string)damage + " anti-armor damage to " + (string)target);
    }
    
    else
    {
        // LBA damage fallback
        string desc = (string)llGetObjectDetails(target, [OBJECT_DESC]);
        if(llGetSubString(desc, 0, 5) == "LBA.v.")
        {
            integer channelLBA = integer("0x" + llGetSubString(llMD5String(target, 0), 0, 3));
            llRegionSayTo(target, channelLBA, (string)target + "," + (string)damage);
            // llOwnerSay("Applied " + (string)damage + " LBA damage to " + (string)target);
        }
    }
}

// Alternatively, if you have llDetectedType available, such as from a collision or a sensor,
// use that instead of llGetHealth as you can check for DAMAGEABLE which is more accurate
// as it signifies the object can actually process damage
DamageArmorDetected(integer index, float damage)
{
    key target = llDetectedKey(index);
    integer detectedType = llDetectedType(index);

    // Can process damage, so apply ANTI_ARMOR type
    if(detectedType & DAMAGEABLE) llDamage(target, damage, DAMAGE_TYPE_ANTI_ARMOR);
    
    else
    {
        // LBA damage fallback
        string desc = (string)llGetObjectDetails(target, [OBJECT_DESC]);
        if(llGetSubString(desc, 0, 5) == "LBA.v.")
        {
            integer channelLBA = integer("0x" + llGetSubString(llMD5String(target, 0), 0, 3));
            llRegionSayTo(target, channelLBA, (string)target + "," + (string)damage);
        }
    }
}
