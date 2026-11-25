/* Pip's voice: babbling chirps synthesized on the fly, no samples. Pure C.
   The UI calls voice_say(); the audio side pulls PCM with voice_render() from its own task/callback. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    VOICE_POKE,    /* "eep!" */
    VOICE_HAPPY,   /* rising chirps */
    VOICE_SAD,     /* slow falling "uuu" */
    VOICE_GROWL,   /* annoyed / angry */
    VOICE_OH,      /* surprised "oh!?" */
    VOICE_LOVE,    /* warbly trill */
    VOICE_GIGGLE,  /* while petted: a happy little giggle */
    VOICE_NOM,     /* eating */
    VOICE_BURP,    /* too full */
    VOICE_YAWN,    /* going to sleep */
    VOICE_DIZZY,   /* wobbly glide */
    VOICE_DERP,    /* shaken silly */
    VOICE_HM,      /* curious */
    VOICE_CENSOR,  /* swearing: garble and a beep */
    VOICE_ALARM,   /* the timer's little song */
    VOICE_HMPH,    /* doesn't like you */
    VOICE_TICK,    /* a detent click: the menu wheel passing a row */
    VOICE_TOCK,    /* Pip drumming a beat back: a short round "tok" */
    VOICE_SNEEZE,  /* hay fever: a rising "ah-ah", then "tchoo" */
    VOICE_RING,    /* an incoming call: a soft warbling ringtone (the interface's, like the timer's song) */
    VOICE_WHEE,    /* excited, swung about: a rising "wheee!" */
    VOICE_HUM,     /* dreamy: a soft hummed "mm-mm-mmm" tune */
    VOICE_POUT,    /* pouty: a puffed, closed-mouth "mf!" and a sulky "mm" */
    VOICE_TADA,    /* proud: a little "ta-daa!" */
    VOICE_THINK,   /* focused: three soft blips, a little robot thinking */
    VOICE_AWW,     /* touched: a breathy, wavering "aww" */
    VOICE_PEEK,    /* playful, peekaboo: a cuckoo's "koo-koo!" */
    VOICE_PHEW,    /* relieved: a breath out, "phew" */
    VOICE_LAUGH,   /* laughing, tickled: "ha-ha-ha-ha", rounder and lower than the giggle */
    VOICE_THANKS,  /* grateful, fed when hungry: a purred "mrr" and a bright "nya!" */
    VOICE_LOOP,    /* tumbling, turned over and over: a loop-the-loop "wheee-oo, wheee-oo" and a giddy "hi-hi" */
    VOICE_BIRTHDAY, /* the owner's birthday: "Happy Birthday to You", its first two lines (a public-domain tune), ~3.5 s */
    VOICE_RANT,    /* berserk: a squeaky gabbled tantrum bleeped three times over, and a strangled "aargh" */
    VOICE_SNORT,   /* a snort through the nose: booped, or blown dry and liking it - "pff-hng" */
    VOICE_BRR,     /* cold: a shivering lip-trill "brrr", teeth chattering */
    VOICE_LULLABY, /* rocked: a slow, sleepy hummed lullaby, falling and fading */
    VOICE_CHAT,    /* talking to himself, glad: a bright bouncy babble, rising and falling like a sentence */
    VOICE_MUTTER,  /* cross: a low grumbling mutter under his breath */
    VOICE_MUMBLE,  /* glum: a slow, falling, half-swallowed mumble */
    VOICE_TEASE,   /* cheeky: a mocking bleated "be-be-be!" */
    VOICE_NOTE0,   /* "Повтори за мной": four sung "doo"s, one a direction (up, right, down, left), C E G C: always the */
    VOICE_NOTE1,   /* same pitch, so a run of them is a tune to remember */
    VOICE_NOTE2,
    VOICE_NOTE3,
    VOICE_FLAP,    /* Полёт Пипа: a flap - a quick breathy upward "fwip" (the interface's level) */
    VOICE_POINT,   /* Полёт Пипа: a point - an achievement's bright rising "ding-ding!" (the interface's level, fixed pitch) */
    /* the everyday faces' own (moods.h), and two bodily noises */
    VOICE_TAPTAP,  /* impatient: "hm-hm-hm-hm", quick closed-mouth blips */
    VOICE_SIGH,    /* wistful: a breath in, then a long soft "haaah" sinking */
    VOICE_MMM,     /* blissful: a low contented "mmm-hmm" */
    VOICE_OOH,     /* adoring: "oooh~" rising, a sparkly quaver */
    VOICE_SNICKER, /* sly: a breathy, low "heh-heh-heh" */
    VOICE_EHE,     /* sheepish: an embarrassed little "e-he..." */
    VOICE_HUP,     /* determined: a punchy "hup!" */
    VOICE_AHA,     /* eureka: "a-HA!" and a little bell */
    VOICE_UGH,     /* rolling his eyes: a long, flat "uuugh" */
    VOICE_KYA,     /* flustered: a squeaked "kya!" and a giggle */
    VOICE_HIC,     /* a hiccup: an abrupt little "hic!" */
    VOICE_TOOT,    /* a toot: "pfrrt" through the lips, never the same twice (a short "pft", a squeaky one, a long one) */
    /* his birth (the first waking after a clean flash, once in his life): fixed pitch, like the tunes */
    VOICE_SPARK,   /* a spark lights in the dark: one glassy "ting" ringing on */
    VOICE_STARDUST, /* star dust spiralling in: rising pentatonic twinkles, softer to brighter, the last one held */
    VOICE_BLOOM,   /* it flares into him: a breathy rising swell, then C-G-C ringing out */
    /* the newer answers' own (moods.h, after PIP_FLUSTERED) */
    VOICE_MMPH,    /* squished under a palm: a muffled "mmf-mmf!" with the mouth shut */
    VOICE_BOING,   /* stretched: a spring's "boi-oi-oing" */
    VOICE_EEK,     /* overwhelmed: "eek-eek-eek!", quick high squeaks */
    VOICE_PINCH,   /* pinched: a strained "nnngh" squeezed up into a squeaky "ip!" */
    VOICE_YAY,     /* beaming: "ya-aay!" */
    VOICE_NUZZLE,  /* cuddly: a low purry "nnn~" rising and settling */
    VOICE_OOHOOH,  /* eager: "oo-oo-oo!", quick rising pants */
    VOICE_YUM,     /* yummy: "mnyam-mnyam" */
    VOICE_FANFARE, /* triumphant: "da-da-DAA!" */
    VOICE_DEFLATE, /* deflated: a hiss and a long whistle falling "pfffiuuu" */
    VOICE_SQUEAK,  /* sparkling: squeaky-clean "squee-squee" and a ting */
    VOICE_GROGGY,  /* groggy: a slow low "mwuh?.." */
    /* the little scenes with pictures (moods.h after PIP_GROGGY), two sounds each */
    VOICE_WOW,        /* starstruck: "wooo-aaah": up and open, a slow shimmer on top */
    VOICE_TWINKLE,    /* starstruck: little bells tumbling up and down, the last two ringing */
    VOICE_LALA,       /* singing: "la-la-la, la-laa": a waltz he made up, never in quite the same key */
    VOICE_ARIA,       /* singing: a tenor's solo: a scoop up, a long held note swelling in vibrato, a tumble down the scale */
    VOICE_MWAH,       /* kiss: "mmm... CHMOK! aah": lips shut and humming up, the pop, a melting breath */
    VOICE_SMOOCH,     /* kiss: two quick pecks and a shy little "hi" */
    VOICE_HEYYOU,     /* flirty: "he-ey...": low and sidling up, a lazy curl at the end */
    VOICE_WINK,       /* flirty: a tiny "pip" and a glint: ting! */
    VOICE_XYLO,       /* rainbow: seven colours: a xylophone run up the scale, and the top note again */
    VOICE_PRISM,      /* rainbow: a shimmering "aaa-ooo-iii": one breath gliding over three vowels and back */
    VOICE_KNOCK,       /* peeking: hiding: two soft knocks, and a careful "u-u?" */
    VOICE_POPUP,      /* peeking: a slide whistle up out of hiding, and "ba!" */
    VOICE_OM,         /* zen: "ooommm": the mouth slowly closing on a long low note */
    VOICE_BOWL,       /* zen: a singing bowl struck: a soft knock, then the ring circling round and fading */
    VOICE_SNORE,      /* sweetdream: "hrrr... fiu": in through the nose, out in a little whistle */
    VOICE_DREAMSONG,  /* sweetdream: talking in his sleep, sweetly: "nya... mm-myam... fuuu" */
    VOICE_MELT,       /* melting: "aaawwwuuu": sliding down like syrup, wobbling on the way */
    VOICE_COO,        /* melting: a dove's "gu-guu-u" */
    VOICE_CLAPS,      /* applause: little palms: round "pat-pat-pat", two pitches by turns, quickening; no hiss */
    VOICE_BRAVO,      /* applause: "bra-VOO! u-hu!" */
    VOICE_BASSLINE,   /* cool: "doo, doo, da-doom": a walking bass hummed softly, each note eased into, the first the gentlest */
    VOICE_YEAH,       /* cool: an unhurried "mm... yeea-ah" */
    VOICE_WHIMPER,    /* puppy: "u-uu-uuu?": a puppy asking, each a little higher and more hopeful */
    VOICE_YIP,        /* puppy: "yip! yip-yip!" */
    VOICE_FLUTTER,    /* butterfly: wings: a soft trill of two notes drifting up, then settling */
    VOICE_HUSHED,     /* butterfly: breath held: a whispered "ooh...", barely there, and a tiny glad "mi" */
    VOICE_BLUB,       /* bubbles: "blub... blub-blub": bubbles let go under water, each a quick scoop up */
    VOICE_BUBBLEPOP,  /* bubbles: "pok!" and a surprised giggle */
    VOICE_BEEPBOOP,   /* robot: "beep-boop-bip, bleep": steady tones, no glide at all */
    VOICE_SERVO,      /* robot: joints: a motor whirring up, a step, whirring back, and a beep "done" */
    VOICE_PURR,       /* kitty: "mrrr-mrrr": in and out */
    VOICE_MEOW,       /* kitty: "mi-aa-ow": mouth shut, wide open, shut */
    VOICE_BLBLBL,     /* silly: a finger over the lips: "blblblbl-brr!" */
    VOICE_YOYO,       /* silly: "ua-ua-uaa": a voice on a yo-yo */
    VOICE_PFFT,       /* snicker: a laugh that will not stay in, behind shut lips: "mf... mf-mf... mmm-hi!" */
    VOICE_CRACKUP,    /* snicker: "mm-HA! ha-ha-ha-ha, ooh": lower and rounder, the run slides down and slows, a sigh after */
    VOICE_CRACKLE,    /* campfire: the fire, heard from under a blanket: soft round pops of all sizes over now and then a low "whuff" */
    VOICE_COZY,       /* campfire: warm through: "mmm... ha-aah" */
    VOICE_SHOOTING,   /* stargazing: a star falling: a glassy run sliding down the sky, its tail fading */
    VOICE_WISH,       /* stargazing: eyes shut: a whispered wish, and two far-off bells answer */
    VOICE_ZAP,        /* zapped: a spark that does not bite: three bubbly "bi-bi-bi" and a wobbling "bzuu-ip!" up */
    VOICE_POWERUP,    /* zapped: a game's power-up: quick triads climbing, the top one held and shaking with energy */
    VOICE_GULP,       /* sipping: "glup, glup, glup... aaah!": drinking it in, each swallow a little higher */
    VOICE_FILLUP,     /* sipping: filling up: a soft hum rising like a glass under the tap, and a ding at the brim */
    /* his birth: fixed pitch, like the tunes; one theme through them (G C E, D G, C) */
    VOICE_SKYLIT,  /* stars coming out: far slow bells, the theme's first notes lost among them */
    VOICE_HEART,   /* a heart, close: "tu-dum" */
    VOICE_RISER,   /* the galaxy winding up: climbing, quicker and quicker */
    VOICE_GRAND,   /* after the bang: the theme, whole and broad */
    VOICE_ANTHEM,  /* for joy: the theme once more, quick, a flourish on top */
    /* the 38 scenes of Nov 2026 (moods.h after PIP_SIPPING), two sounds each */
    VOICE_HEEHEE,      /* xd: squeezed high "hi-hi-hi-hi" that cannot stop, a gasp, and again */
    VOICE_WHEEZE,      /* xd: out of breath with laughing: "ha-ha... haa... hii" */
    VOICE_ROLLING,     /* rofl: a laugh going round: down one side and up the other, twice */
    VOICE_HOHO,        /* rofl: "o-ho-ho-ho": round and low, from the belly */
    VOICE_BOINGS,      /* bouncy: "bo-ing, bo-ing, bo-ing": three springs, each higher */
    VOICE_YIPPEE,      /* bouncy: "yip-piii!" */
    VOICE_PETALS,      /* blooming: six flowers opening: a soft bell each, round about */
    VOICE_SPRINGSONG,  /* blooming: a little hummed skip: "tra-la-la" */
    VOICE_LUBDUB,      /* hearteyes: a heart: "bu-bum... bu-bum", warmer each time, and a sigh */
    VOICE_SWOON,       /* hearteyes: "aaah": up, held, melting down */
    VOICE_ROCKETUP,    /* fireworks: a whistle up, a hush, and a shower of soft bells */
    VOICE_OOHAAH,      /* fireworks: the crowd: "ooo-ooh... aa-aah!" */
    VOICE_MEDALIST,    /* medal: a short march home: "ta-ta-ta taa, ta-taa" */
    VOICE_MHM,         /* medal: pleased with himself: "mm-hm. Mhmm!" */
    VOICE_DISCOBEAT,   /* disco: "oon-tsa" of the mouth: a low "doom", a light "ti", and a hook */
    VOICE_GROOVY,      /* disco: "ua-ua, ua-ua-uu": a wah-wah */
    VOICE_EYEPOP,      /* popeyed: "hm?.. bwo-OOP!": two eyes out on springs */
    VOICE_WHOANELLY,   /* popeyed: "wo-o-o-ow": down and up again, not believing it */
    VOICE_IDEAPING,    /* lightbulb: three drops of thought... and "ting!" */
    VOICE_GOTIT,       /* lightbulb: "hmm... a! a-ha-a!" */
    VOICE_PEEKSHY,     /* paws: from behind paws: "mi?.. mm!.. mi-hi" */
    VOICE_BASHFUL,     /* paws: "u-u-uh" sinking into his collar */
    VOICE_HELLOO,      /* hello: "pri-vie-et!": up, up and wide open */
    VOICE_YOOHOO,      /* hello: "yu-huu!" */
    VOICE_ROYAL,       /* king: trumpets: "pa-pa-ra-paa" */
    VOICE_HARRUMPH,    /* king: a king clears his throat: "ahem... mmm" */
    VOICE_HALOSONG,    /* angel: a choir of one: "aa-aaa-aa", high and clear */
    VOICE_WHISTLING,   /* angel: an idle whistle: not me */
    VOICE_MWAHAHA,     /* imp: a small villain: "mwa-ha-ha-ha", tiny and pleased */
    VOICE_TEEHEE,      /* imp: a whispered plot: "hi... hi-hi" */
    VOICE_HOPLA,       /* juggler: three throws: "hop, hop, hop-la!" */
    VOICE_CIRCUS,      /* juggler: the ring: a jolly run, oompah */
    VOICE_LIFTOFF,     /* balloon: floating up: a long glide, swaying */
    VOICE_POPFALL,     /* balloon: "pok!", a slide down, "bump", and a giggle */
    VOICE_LICK,        /* icecream: "lyap... mm... lyap... mmm!" */
    VOICE_FREEZE,      /* icecream: "a-y-y-y!" teeth together, shivering, then "fu-uh" */
    VOICE_PITTER,      /* umbrella: rain on the umbrella: soft drops of every size */
    VOICE_DRYHUM,      /* umbrella: dry and pleased: a humming stroll */
    VOICE_HEAVE,       /* strongman: "nnn... nnn... HUP!" */
    VOICE_STRONG,      /* strongman: puffed up: "u-ff... ta-daa" */
    VOICE_ABRACADABRA, /* magician: a spell mumbled, winding up - and "puff" */
    VOICE_RABBIT,      /* magician: sparkles up and a little "a-voila!" */
    VOICE_DAWN,        /* sunrise: light rising: four soft notes stacking up */
    VOICE_STRETCH,     /* sunrise: a good stretch: "mmm-aaa-ahh" and a smack */
    VOICE_BAA,         /* sheep: a small soft sheep: "be-e-e" */
    VOICE_COUNTING,    /* sheep: one... two... thr...: each lower and slower, a yawn */
    VOICE_HIYA,        /* ninja: "ki-yaa!" short and neat */
    VOICE_SHADOW,      /* ninja: sneaking: five toes, and a soft gong */
    VOICE_GROWING,     /* sprout: growing: a creeping glide with three little leaves */
    VOICE_BLOSSOM,     /* sprout: "pu-uh!" it opens, and bells */
    VOICE_GREYSIGH,    /* raincloud: a long grey sigh: "haah... mm" */
    VOICE_DRIZZLE,     /* raincloud: drops falling in minor: one, two... three */
    VOICE_BAWL,        /* sobbing: "u-aaa... u-aaa-aa": crying out loud */
    VOICE_SOBS,        /* sobbing: catching his breath: "hu-hu-huu... hik" */
    VOICE_CRACKED,     /* heartbroken: "mm?.. krr-ak" and a voice going down the stairs */
    VOICE_LETGO,       /* heartbroken: an unsteady "a-ah..." trailing off */
    VOICE_KETTLE,      /* fuming: a kettle coming to the boil: a murmur, then a round whistle up */
    VOICE_GRRMPH,      /* fuming: "grr-rr... mpf!" */
    VOICE_SMOULDER,    /* burning: low coals: a slow growl that flares */
    VOICE_ROARLET,     /* burning: a small fierce "rrraa!" */
    VOICE_HMFF,        /* sulking: "hmf!" and his nose in the air */
    VOICE_NYEH,        /* sulking: "nye-e-e": not looking at you */
    VOICE_TEETH,       /* spooked: teeth chattering softly, and a wavering "u-u" */
    VOICE_BOOO,        /* spooked: the ghost: a hollow "u-uuu-uu" */
    VOICE_EMPTYWIND,   /* tumbleweed: wind over nothing: a slow rise and fall, and a far whistle */
    VOICE_MEH,         /* tumbleweed: "me-e-h" */
    VOICE_POWERDOWN,   /* lowbatt: winding down: each note slower and lower */
    VOICE_LOWBEEP,     /* lowbatt: "bip... bip... bu-up": an empty battery */
    VOICE_RUMBLE,      /* hungry: a belly: "gru-u-ull", and an embarrassed "u?" */
    VOICE_NOCOOKIE,    /* hungry: "o-oo!.. oh...": there it is - and it is gone */
    VOICE_BIRDIES,     /* knockout: little birds going round: "tiu-tiu, tiu-tiu" */
    VOICE_KAPUT,       /* knockout: "bo-nk... uuu-ooo": and over he goes */
    VOICE_LOVESMENOT,  /* wilted: loves... loves not... loves... not */
    VOICE_FADING,      /* wilted: a music box running down */
    VOICE_SNIFFLE,     /* sniffles: "shmyg... shmyg": a stuffy little sniff up, twice, and "mm" */
    VOICE_ACHOOLET,    /* sniffles: "ah... aah... pchi!" small and through the nose */
    /* the cable pulled out (Nov 2026) */
    VOICE_POPFULL,     /* charged: "pok! - ah" and a ding, half a second */
    VOICE_CHARGEDUP,   /* charged: calm: the plug easing out, a muffled click, a content "mm" */
    VOICE_YANK,        /* cutoff: "pok!" - and a startled "eh?!" going up */
    VOICE_NOTFULL,     /* cutoff: two low blips and a pouty "uh" */
    VOICE_POWERFAIL,   /* empty: a soft whine sliding down to nothing, a TV going off */
    VOICE_FEEBLE,      /* empty: a weak little "mm..." flickering back on */
    VOICE_RETCH,       /* queasy: a gulp held in, then a rough "hweh-uhp" - short */
    /* New Year (Nov 2026): the bells keep their pitch and pace (pip.c times the scenes to them) */
    VOICE_JINGLE,      /* newtree: sleigh bells shaken, a little bell tune, and a ding as the star lights (~1.7 s) */
    VOICE_OHTREE,      /* newtree: a hum looking it over, then "o-oh!" as the star lights, and a ding */
    VOICE_HANGUP,      /* wreath: a whistle sliding down as it comes, a soft "tok" as it lands, bells as its lights come on (2 s) */
    VOICE_AAH,         /* wreath: "o?" up at it, "oo" following its swing, then an admiring "a-aah~" and a ding as it lights up */
    VOICE_BABBLE,  /* his talk: a phrase made to order by voice_babble() - how he feels, how worked up, what kind */
    VOICE_COUNT
};

/* the kinds of phrase voice_babble() makes */
enum {
    BABBLE_SAY,      /* a plain sentence: up and down, falling at the end */
    BABBLE_ASK,      /* a question: rising at the end */
    BABBLE_EXCLAIM,  /* an exclamation: high, quick, the last syllable up and long */
    BABBLE_COMPLAIN, /* a grumble: low, rough, falling */
    BABBLE_SOB,      /* a sorry tale: wavering, breathy, a catch in it */
    BABBLE_WOOZY,    /* still giddy: the line swims up and down */
    BABBLE_GIGGLY,   /* glad and bubbling over: a sentence with giggles in it */
    BABBLE_INTERJ,   /* one sound: "o!", "m?", "eh" - a feeling noted, not a speech */
    BABBLE_KINDS
};

void voice_init(int sample_rate);
void voice_say(int sound);       /* replaces whatever is playing */
void voice_on_say(void (*cb)(void)); /* the host's audio side is told a sound is due (it sleeps while he is quiet) */
/* his talk: kind BABBLE_*, valence -1 (glum, cross) .. 1 (glad), arousal 0 (calm, tired) .. 1 (worked up); replaces what
   is playing, like voice_say */
void voice_babble(int kind, float valence, float arousal);
/* how he sounds just now, for the next of his own sounds (not the interface's, nor the notes, the song or the drums):
   semitones up or down (-3..3) and a tempo factor on every length (0.8 brisk .. 1.25 slow) */
void voice_tone(float semis, float tempo);
void voice_set_volume(float v);  /* 0 = mute .. 1 */
float voice_volume(void);
/* the Звуки settings: how loud his own voice is, and the interface's (the menu's clicks, the timer's song, the ring),
   each 0 (off) .. VOICE_STEPS (as loud as it goes) */
#define VOICE_STEPS 5
#define VOICE_UI_DEFAULT 3 /* after a clean flash: the interface at 60%, his voice full (VOICE_STEPS) */
void voice_levels(int pip, int ui);
int voice_pip_level(void);
int voice_ui_level(void);
/* a soft chord held under everything (his birth): up to 5 notes, Hz, at a level 0..1, reached in about `secs`, one chord
   flowing into the next; n 0: fades out */
void voice_pad(const float *hz, int n, float level, float secs);
bool voice_busy(void);
void voice_hush(void);          /* the sound playing (and one asked for) stops at once: a 30 ms fade, no click */
int voice_started(void);         /* how many sounds have started so far (pipsim's status: a crackle of restarts shows) */
/* Fill n mono 16-bit samples (silence when idle); returns true if anything was audible. */
bool voice_render(int16_t *out, int n);
const char *voice_name(int sound);
