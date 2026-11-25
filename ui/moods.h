/* Pip's moods. Plain C so the pet logic can pick them without LVGL. */
#pragma once

enum {
    /* the first twelve have debug keys 1..9 0 - = */
    PIP_NEUTRAL, PIP_HAPPY, PIP_SAD, PIP_ANGRY, PIP_SURPRISED, PIP_SLEEPY, PIP_ASLEEP, PIP_SKEPTICAL,
    PIP_CURIOUS, PIP_LOVE, PIP_DIZZY, PIP_OUCH,
    /* body */
    PIP_STONED, PIP_PINKEYE, PIP_NAUSEOUS, PIP_SICK, PIP_TIRED, PIP_EXHAUSTED,
    /* anger, from mild to red */
    PIP_ANNOYED, PIP_FURIOUS, PIP_RAGE, PIP_SWEARING, PIP_INDIGNANT, PIP_BELLIGERENT, PIP_HURT,
    /* dark side */
    PIP_DISGUSTED, PIP_SUSPICIOUS, PIP_CYNICAL, PIP_AFRAID, PIP_NERVOUS, PIP_DESPONDENT, PIP_LONELY, PIP_GLOOMY, PIP_DULL,
    PIP_SHY, PIP_SMUG, PIP_BORED, PIP_SHOCKED, PIP_DISMAYED, PIP_PUZZLED,
    /* bright side */
    PIP_AMAZED, PIP_CHEEKY, PIP_CONTENT, PIP_COMFY,
    /* answers: a nod, a head shake */
    PIP_YES, PIP_NO,
    /* rare cards beside stoned and pinkeye (pet.c deals them by what his day has been like) */
    PIP_ALLERGIC, PIP_FROZEN, PIP_OVERHEATED, PIP_SMITTEN, PIP_HYPNOTIZED, PIP_HYPER, PIP_ZOMBIE,
    /* feelings things you do to him bring out (pip.c): swung, a finger resting on him, teased, the day's walk done,
       a finger to follow, long stroking, peekaboo, the world the right way up again, tickled, fed when hungry */
    PIP_EXCITED, PIP_DREAMY, PIP_POUTY, PIP_PROUD, PIP_FOCUSED, PIP_TOUCHED, PIP_PLAYFUL, PIP_RELIEVED, PIP_LAUGHING, PIP_GRATEFUL,
    PIP_DANCING, /* music with a beat on the mic (mic.c mic_music): he bobs and nods in time */
    PIP_TUMBLING, /* turned upside down and back, again and again: giddy with it, a loop-the-loop "wheee-oo" */
    PIP_BERSERK,  /* past swearing: he loses it altogether - red, mismatched pinpoint eyes darting, shaking, a hail of
                     grawlix and a censored tantrum (VOICE_RANT) */
    PIP_WHIRLED,  /* a spiral or circles drawn on him: his pupils ride round with the finger (pip.c spiral_feed) */
    PIP_WOOZY,    /* spun round and let go: the pupils go on round by themselves, slowing, his eyes out of step */
    /* everyday faces, seen often (the day's palettes, his own moments, answers to touch): springs, tints and the renderer's
       own features only - no new per-pixel work */
    PIP_IMPATIENT,  /* waiting for you: quick glances at you and away, a tapping little nod */
    PIP_WISTFUL,    /* looking into the distance, a little sad and a little glad, slow */
    PIP_BLISSFUL,   /* eyes squeezed into happy arcs, blushing, breathing slowly */
    PIP_ADORING,    /* big shining pupils on you, a blush */
    PIP_SLY,        /* narrowed eyes and a grin, looking aside: up to something */
    PIP_SHEEPISH,   /* caught out (a burp, a toot, the sponge flung): eyes down and aside, blushing, peeking up at you */
    PIP_DETERMINED, /* flat lids, a hard look straight ahead: game on */
    PIP_EUREKA,     /* "aha!": eyes wide, looking up, a yellow "!" */
    PIP_EYEROLL,    /* rolls his eyes: the gaze sweeps up and over (pip.c update) */
    PIP_FLUSTERED,  /* a red blush, quick blinks, eyes darting */
    /* answers to what you do, newer (pip.c): a palm, two fingers, and the other faces of a joy, a meal, a
       game's end, a bath and a waking - springs and the renderer's features only, like the rest */
    PIP_SQUISHED,   /* a palm laid over him: both eyes squashed flat and wide, squirming (multi-touch) */
    PIP_STRETCHED,  /* two fingers on his eyes drawn apart: eyes pulled wide, pupils outward, "boing" */
    PIP_OVERWHELMED, /* two fingers on him (not his eyes): too many! tiny pupils darting from one to the other, a sweat drop */
    PIP_PINCHED,    /* two fingers on his eyes drawn together: the eyes squeezed in to his nose, crossed, then a pop */
    PIP_BEAMING,    /* joy's other face: big round smiling eyes with a shine, bouncing */
    PIP_CUDDLY,     /* stroked: leans right into your hand, eyes half shut, a blush */
    PIP_EAGER,      /* a biscuit coming: looking up for it, quick little nods */
    PIP_YUMMY,      /* munching: eyes in happy arcs, chewing nods */
    PIP_TRIUMPHANT, /* he beat you: chin high, a shine, a hop */
    PIP_DEFLATED,   /* you beat him: the eyes shrink and droop like a let-down balloon */
    PIP_SPARKLING,  /* scrubbed clean: shining eyes, a fresh pale tint */
    PIP_GROGGY,     /* woken by you: one eye still shut, swaying, unfocused */
    /* little scenes with pictures round his eyes (ui/sprite.c: hearts, stars, notes... each a small ready-made A8 picture),
       played by pip.c show_face; kind faces all, two sounds of their own each. At most 254 moods: pet_t keeps them in bytes */
    PIP_STARSTRUCK, /* gold stars for pupils, sparks round them */
    PIP_SINGING,    /* eyes shut, swaying to it, coloured notes floating up */
    PIP_KISS,       /* leans in, eyes shut, and a big heart comes at you with three small ones */
    PIP_FLIRTY,     /* a sidelong look, then a wink with a star off it */
    PIP_RAINBOW,    /* his eyes run through every colour, beads of them going round the rim */
    PIP_PEEKING,    /* hides under the lower edge, peers left and right, ducks, pops up: ta-da! */
    PIP_ZEN,        /* afloat, eyes shut, breathing, three little planets round him */
    PIP_SWEETDREAM, /* asleep and happy: blushing, "z z z", a bubble swelling under his nose */
    PIP_MELTING,    /* melts with tenderness: eyes spreading, dripping, hearts over them - and gathers himself again */
    PIP_APPLAUSE,   /* claps his eyes together like hands, stars off every clap, two hops */
    PIP_COOL,       /* sunglasses: blue lenses in gold, nodding to a beat, a gleam across the glass */
    PIP_PUPPY,      /* puppy eyes, looking up, the head to one side, then the other */
    PIP_BUTTERFLY,  /* follows a butterfly; it sits down between his eyes, he goes cross-eyed and smiles, it flies off */
    PIP_BUBBLES,    /* watches soap bubbles rise; each pops, a blink, a laugh */
    PIP_ROBOT,      /* square pupils, an aerial with a lamp, moving in steps */
    PIP_KITTY,      /* ears and slit pupils, kneading, a slow blink: a cat's "I love you" */
    PIP_SILLY,      /* fooling about: eyes swapping sizes, crossing and parting, the head lolling */
    PIP_SNICKER,    /* holds a laugh in with all his might, then it bursts out of him */
    PIP_CAMPFIRE,   /* a little fire below him, its light flickering on his eyes, embers rising */
    PIP_STARGAZING, /* the night sky: twinkling stars, a falling one he follows, a wish with eyes shut */
    PIP_ZAPPED,     /* the cable in: a bolt strikes, lit up, wide and shaking - then bouncing, full of beans */
    PIP_SIPPING,    /* the cable in, thirsty: drinks the charge in gulps, pale turning to a warm green glow, "aah" */
    /* 38 more little scenes with pictures (Nov 2026; show_face): 25 kind, then 13 sad or cross from PIP_RAINCLOUD */
    PIP_XD,             /* helpless with laughter: thin squeezed crescents shaking, tears flying; a snort pops them open */
    PIP_ROFL,           /* rolling on the floor: the head over to one side, to the other, shaking */
    PIP_BOUNCY,         /* bounces like a ball; every fourth hop a somersault */
    PIP_BLOOMING,       /* flowers open one after another above and below him; then their petals fly */
    PIP_HEARTEYES,      /* a heart grows over him and beats, he melts at each beat; it bursts into little hearts */
    PIP_FIREWORKS,      /* a rocket, the eyes after it, a burst - three times */
    PIP_MEDAL,          /* a gold medal on him: a look down at it, chin up, it gleams */
    PIP_DISCO,          /* a mirror ball, spots of light sweeping the floor, and he is dancing */
    PIP_POPEYED,        /* looks, looks again - and his eyes pop out of his head */
    PIP_LIGHTBULB,      /* hm... hm... three dots - and a bulb lights up over him */
    PIP_PAWS,           /* hides his eyes in his paws, peeks round one, hides, peeks again - and comes out, blushing */
    PIP_HELLO,          /* a paw waves, the head to one side; a hop and a wink */
    PIP_KING,           /* a crown: haughty looks; it slips down over his eyes, he tosses it back */
    PIP_ANGEL,          /* a halo, the most innocent eyes there are; a glance aside, whistling: who, me? */
    PIP_IMP,            /* little horns, a grin: he is up to something - rubs along side to side and snickers */
    PIP_JUGGLER,        /* three balls thrown from eye to eye; each catch squashes it, the look follows the highest */
    PIP_BALLOON,        /* a balloon on a string lifts him, swaying; pop! he drops, blinks - and laughs */
    PIP_ICECREAM,       /* two scoops: a lick, a lick - brain freeze! - and bliss */
    PIP_UMBRELLA,       /* rain all round and he is dry under an umbrella, swaying, pleased */
    PIP_STRONGMAN,      /* a barbell: up... it won't... up over his head! holds it shaking, drops it, proud */
    PIP_MAGICIAN,       /* looks into a top hat, abracadabra - puff! bunny ears come up out of it */
    PIP_SUNRISE,        /* the sun comes up below him; he stirs, squints, stretches - good morning */
    PIP_SHEEP,          /* counts sheep over a fence; heavier and heavier, nods off, catches himself */
    PIP_NINJA,          /* a red headband, narrowed eyes; a shuriken - dodged! another - dodged! a cool look */
    PIP_SPROUT,         /* three drops of water, and a sprout comes up on his head: a stem, leaves, a flower. He adores it */
    PIP_RAINCLOUD,      /* a cloud of his own, raining on him; a look up at it, and down again */
    PIP_SOBBING,        /* the waterworks: streams from shut eyes, a puddle rising below */
    PIP_HEARTBROKEN,    /* a heart over him beats - cracks - its halves fall; he watches them go */
    PIP_FUMING,         /* holds it in, reddening and swelling - and steam whistles out at both sides */
    PIP_BURNING,        /* so angry his eyes are on fire */
    PIP_SULKING,        /* turned his back on you: a glance over, "hmf", and away again */
    PIP_SPOOKED,        /* dark: only his pupils glow, darting. A ghost! - eyes wide - it goes by, dark again */
    PIP_TUMBLEWEED,     /* nothing happens. A tumbleweed rolls by. He watches it go. A sigh */
    PIP_LOWBATT,        /* run down: an empty battery blinks over him, the lids sink, he jerks up and sinks again */
    PIP_HUNGRY,         /* dreams of a biscuit: it hangs in a cloud over him, gets bitten - and is gone */
    PIP_KNOCKOUT,       /* out for the count: heavy uneven lids, pupils drifting round, cross-eyed, stars going round */
    PIP_WILTED,         /* a daisy: its petals go one by one, and with them his hopes */
    PIP_SNIFFLES,       /* a cold: a red nose, a striped scarf, sniff... ah... ah... a little sneeze */
    /* the cable pulled out (Nov 2026): by the charge he got */
    PIP_CHARGED,    /* 80% or more: the plug pops out, four green pulses of charge flash through his eyes, a proud stretch */
    PIP_CUTOFF,     /* 30..79%: the plug yanked away, his eyes after it; they go dim and flicker, he leans after it and sighs */
    PIP_EMPTY,      /* under 30%: the plug yanked, his eyes go out like an old TV - a line, a dot - then flicker back on, weak and grey */
    PIP_QUEASY,     /* too much food: green, swaying, his cheeks puff - a retch - woozy, a sweat drop */
    /* New Year (Nov 2026; show_face, sprites only) */
    PIP_NEWTREE,    /* a little fir below him, its lights running, snow falling; the star on top lights - wow - and he beams */
    PIP_WREATH,     /* a Christmas wreath with a red bow comes down on a ribbon, swinging, his eyes after it; it settles, its
                       lights come on - wow - and he sways with it, beaming, snow falling */
    PIP_MOOD_COUNT
};
