/* ============================================================
   EGG n SOY  -  an action puzzle climbing game
   ------------------------------------------------------------
   Pure Win32 C. No external libraries to link against.
     - graphics : GDI + DIB section, art re-sampled off the
                  reference sheet into the source
     - sound    : waveOut, software chiptune synth (2 pulse +
                  noise + sfx channel) with an original score
     - music    : sounds/piko1.mp3 on the title and sounds/neon1
                  and neon2.mp3 on the odd and even stages, if that
                  folder is beside the exe, play over the synth
                  score. media
                  foundation decodes them and is loaded at run time,
                  so the exe still starts - and still sounds like
                  itself - with no files and no mfplat at all.
   build:
     gcc -O2 -o eggnsoy.exe game.c -lgdi32 -lwinmm -mwindows
   ============================================================ */

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <mmsystem.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

/* ------------------------------------------------------------------ */
/*  constants                                                          */
/* ------------------------------------------------------------------ */
#define TILE    32
#define MAPW    64                       /* the world is 64 tiles wide  */
#define MAPH    17
#define VIEWW   30                       /* 30 of them fit on screen    */
#define HUD_H   40
#define SCR_W   (VIEWW*TILE)             /* 960 */
#define SCR_H   (MAPH*TILE + HUD_H)      /* 584 */
#define WORLD_W (MAPW*TILE)              /* 2048 - and it wraps round   */
#define CAM_LAG     0.25f                /* how hard the camera chases  */

#define AW      22      /* actor collision box */
#define AH      28

/*  four decks, stacked every four rows: solid ground at row 16 and one
    way platforms at rows 12, 8 and 4. head room inside a deck is three
    tiles, and a full jump rises 138px - ten pixels more than the four
    tiles it takes to land on the platform above, so the deck overhead
    is reachable by holding the button and nothing less.               */
#define GRAV_UP     0.50f      /* while rising  */
#define GRAV_DN     0.62f      /* while falling */
#define MAXFALL    13.0f
#define PSPEED      3.1f
#define PJUMP     -12.0f
#define JUMPCUT    -3.6f       /* released early -> shorter hop */
#define PCLIMB      2.8f
#define COYOTE      8          /* frames of grace after leaving a ledge */
#define JBUF       10          /* frames a jump press stays remembered  */
#define INVULN     150         /* mercy frames after a respawn          */
#define DROPTHRU   12          /* frames DOWN keeps you falling through */

/* enemy contact needs this much overlap - generous on purpose */
#define HITX        6
#define HITY        7

#define LIFE_MAX    3          /* hearts, refilled at every stage */
#define HURT_INV   100         /* mercy frames after taking a hit */

#define KICK_DUR    14         /* whole kick animation            */
#define KICK_ACT     9         /* frames the foot can connect     */
#define KICK_CD      2         /* wind down before the next kick  */
#define KICK_W      30         /* how far the foot reaches        */
#define KICK_H      20
#define STUN_LEN   120         /* two seconds of seeing birds     */
#define KNOCK    (2*TILE)      /* kicked back two tiles           */
#define KB_LEN      12         /* frames the knock back takes     */
/*  the slide eases out: the step shrinks by KB_STEP every frame, so the
    distance covered is KB_STEP * (12+11+..+1) = KB_STEP * 78 = KNOCK.  */
#define KB_STEP  (KNOCK * 2.0f / (KB_LEN * (KB_LEN + 1)))

/*  dash: three tiles in a fifth of a second, then a second of wind down.
    96 / 12 = 8px a frame, a bit over twice the walking speed.          */
#define DASH_LEN    12
#define DASH_SPD    (3.0f * TILE / DASH_LEN)
#define DASH_CD     14         /* barely a breath before the next  */

#define COMBO_WIN  120         /* two seconds to chain the next fruit */
#define MAXPOP      12         /* score popups on screen at once      */
#define POP_LIFE    46

#define MAXGHOST     8         /* dash afterimages                    */
#define GHOST_LIFE  14
#define MAXPUFF     30         /* dust and damage sparks share a pool  */
#define PUFF_LIFE   18

/*  taking a hit is the one thing that must never be missed, so it gets
    the full treatment: a few frozen frames, a shaken screen, a red
    wash over the field and a shower of sparks off the player.       */
#define HURT_FX     26
#define HURT_SHAKE  11
#define HURT_FREEZE  5

/*  the player is the blue of the reference sheet; the foes get the
    pink and green from the same sheet first, so the one blue foe in
    three never sits next to the player without the scarf to tell
    them apart.                                                      */
#define PC_MAIN  0x2F73FD
#define PC_DARK  0x0738B8

#define MAXENEMY  12
#define NSTAGE     6

/* ------------------------------------------------------------------ */
/*  pixel art                                                          */
/* ------------------------------------------------------------------ */
/*  '.'transparent 'k'outline 'w'white 'e'pupil
    'c' main color   'C' dark color   (supplied at draw time)
    fixed: r/R red  g green  n/N brown  y yellow  o orange
           b blue   E gray   W light gray   D dark gray            */

/* ------------------------------------------------------------------ */
/*  character art                                                      */
/* ------------------------------------------------------------------ */
/*  the player and the foes come straight off the reference sheet. it
    was drawn at three screen pixels to the dot, so every frame is
    sampled back down onto its own grid and the soft shading folded
    into one shared palette - re-sampled, not redrawn.

    a frame is one char a pixel: '.' is see through and every other
    printable character indexes ART_PAL. each frame also carries the
    offset from the actor box that places it, so the head holds still
    and the feet stay on the floor whatever the pose - which is what
    lets the walk cycle, the jump and the kick share one anchor.    */

#define ART_NPAL 90
static const unsigned ART_PAL[ART_NPAL] = {
    0xF9FAFB, 0xDBE6F2, 0xF8E8DE, 0xF6ADCF, 0xFDF079, 0xFCE416,
    0xB7D4F8, 0xBCBFCB, 0x51C4AB, 0x09B78F, 0xF79608, 0xF8C506,
    0xC5840B, 0x99BCFA, 0x74A4F8, 0x5E94FA, 0x06A080, 0x038670,
    0x4B89FC, 0x4081FB, 0x8F8D9B, 0xBE77CF, 0x8670DF, 0x3A7BFC,
    0x5064A9, 0x3578FD, 0x3276FD, 0x5278EA, 0x2E7AFA, 0x3075FD,
    0x2F73FD, 0x2E73FD, 0x2E6FF3, 0x2D71FD, 0x2C64E2, 0x2B72FD,
    0x2A6FFD, 0x266DFC, 0x2269FA, 0x1560F7, 0x1D64F7, 0x1059F3,
    0x1A5DEF, 0x017061, 0xF071AF, 0xFC3781, 0x9F5D08, 0xE2226C,
    0xC0135A, 0x494758, 0x7C460B, 0x1052E5, 0x5B5677, 0x5C3B11,
    0x950F4B, 0x2E2A3D, 0x5F0D38, 0x0950F0, 0x0547E8, 0x0B48D5,
    0x1E53C8, 0x063CC9, 0x0738B8, 0x072C8E, 0x051D66, 0x131F44,
    0x023F3B, 0x191C2A, 0x0A122F, 0x32131A, 0x190B1A, 0x033EDD,
    0x2248A8, 0x0631A5, 0x02564F, 0x072478, 0x051751, 0x022A2B,
    0x030B31, 0x04113F, 0x080D1F, 0x030925, 0x031C1D, 0x03061B,
    0x020414, 0x020F13, 0x02040E, 0x020209, 0x10070F, 0x010104,
};
/*  ART_PAL[i] is written as ART_KEY[i]; '"', '\\', '.' and '?' are
    left out - the first two cannot sit in a string, '.' is the
    transparent dot and '?' would risk a trigraph.                */
static const char ART_KEY[] = "!#$%&'()*+,-/0123456789:;<=>@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_`abcdefghijklmnopqrstuvwxyz{|}~";

static const char *ART_IDLE[36] = {
    "............ffeeff..............",
    ".........vuxuxuuuuuviiiv........",
    "........exy{{yyyxxxxxvvxt.......",
    ".....iyyx`WMMMNKKLKKMMMbv|~.....",
    "....{xccaLJHHHHGGGHIJJLNcnpy....",
    "...uedWLEBCCBBBCCCCEEHIIJM`e|...",
    "..q{l^LGBBBBBBBBBCCEEEEECCK^txt.",
    ".vvnMHAAABBBBBBBBBBCCCCBA==LMtf.",
    ".it^I<==ABBBBBBABBBBBBAA=<<AKqi.",
    "vqdM<<<=ABBBBBAABBBBBA==<<<<L`qx",
    "xp_N=<<<::<EBABBBBCCBA=<556ALMqx",
    "ye_IC=6(!!#:AABBCEECAAA1!!!2K^qy",
    "ye_JC:(!!7XXa=BCBCCCA=FXX7!#:Mqx",
    "ye_IC0!!!ivvm<ABBBBCB=avvf!!(Mqx",
    "ye_IA0!!!ivvm=ABBBCCGBasvf!!(Mqx",
    "xe_H=0!!!issm<BCBBCCEBasvf!!(Mqv",
    "xe_IB0!!!issm<BCBBBCB=avxf!!(Kqv",
    "xe_JH1!!!Xssm=BCCCCBA<axuf!!(Kqv",
    "xe_LD8$!!#))>BCEEEECA=2))#!!8Ntx",
    "xelK9P%!!!!(=EEEEEEBA=6#!!!%PFty",
    "vtpM>PP%!!06EBBCECBAA=A6#!%%8dvu",
    ".ut_J9996:EECBBBBBA===AG629>Fqi.",
    "..yt^NDEEBAAABCEEBBAAAACILMbqvi.",
    "...ippdcWJHHILLNNNLLJJINWcpe|...",
    "....{~|tddpppppeeppppppppt|uf...",
    "...f{ssvy{}}}}}}}}}}}}}||tsi....",
    "...yqbKKqj-------------Vy^cyi...",
    "..ut`IKMduYR--'''''''-Vjy^^`xx..",
    "..|d^@L`veWXYR,''''-RVmdx^W`xy..",
    "..|tcW`ev`KFm;VRRRVVXaMpx^bt|...",
    "..ixteqsdNJ::DUUUUUUWWbqyqt|i...",
    "...iyx{s^I62:JNMMM^bbcnnpyuu....",
    "......isWL=D``nnnnnqecKWdy......",
    "......uysc``bn~~|{ypaNWnt|......",
    ".......iyyy{{{hf.fxxxy{|y.......",
    "........iiiiu.....iiiiiu........",
};
static const char *ART_W0[36] = {
    "fffffffffffffffttttffffff..........",
    "...............fffftttt............",
    "..............u{{{{{|||||{{e.......",
    "...........||{pnnnnnnnnnncnvy{.....",
    ".........vuqpp`NLLJLLLLLLNWdptv....",
    "........fvebWIGGCECEEECEEECIJciu...",
    "........xnMJGCCCCCCCCCBBBCCCGKnqy..",
    "......u{dMJCAAAAAABBBAAAAABCCI^dx..",
    "......xe^MCCA==<==AAAAABBBBBAAHbei.",
    "......spMIGCCA<<<<==ABBCCBBA==GMb{.",
    ".....yn^MEEEEA<:666:=BBECCC=<65L`{.",
    ".....yn^ICCEEB=1!!!(:ABBBBA=6!!1by.",
    ".....yd_IBCCEC2!!!UUeF<AAA=<#!Uhcy.",
    ".....yd_IBCCBA0!!$yxvF:==<<6!!Uyc{i",
    ".....yd_JCCCA=(!!!yxxF===<<6!!Uyn|i",
    ".....yd_JCECAC0!!!yyyF==<<:6!!U{n~u",
    ".....{d_JEEEEH0!!!x{yFA==<:5!!U{n~u",
    ".....{d_LHEEEH0!!!7U;DBB=<:5!!7;;|.",
    "....[|n_^IHH:98$!!!!(==A==<=(!!%;{.",
    ".VVRV}td^KHH>PPP$!!(:<<===BG:(%P;{.",
    "V/,-,,jt_^KH:8PP822<<====AACB6>mxu.",
    "V--'''VuebMLJDD:6:<BBBA=====HL`p~..",
    "VR/'''-V}edb^WKKLLLKKKLLLJINMcexh..",
    ".VR-'--/Y{yennnndddddddddnnccsvh...",
    "..VR--R|tb`dtyy{~~~~||}}}}}|x|f....",
    "...YVVjqcI@LbvXR/--------,R{_nsu...",
    "....Yj}d<5<KcvaXR/,-''''-/Y{^bsy...",
    ".....h{d62LbpqLDUU//,,,,/Uey^dsu...",
    "......hxda`qt^LIDDUYYYYYUbn|ev.....",
    ".......iyy{tcKA216Faaamalet~ui.....",
    ".........y|dW@1##2aMKM_bbdt~.......",
    ".........{tcb@5211qplbpdlld~.......",
    ".........ysnc`_``^e{ysl_^nt|.......",
    "..........f{vdbcdsy|sb_ldsy........",
    "...........uuxssv{uiisssxx.........",
    ".............yyxy....uuxy..........",
};
static const char *ART_W1[35] = {
    "............vvxxxvvxxxxx........",
    "...........sy{{{{{yyyxxxf.......",
    "........y||dlMMMMMMKKKK^s{|.....",
    "......iuqncWNIIHHHHHHHHJacd{u...",
    "......xdbNHCEBAAAAABCEEEHLWeyu..",
    ".....{d^JBCBBAA=AAABCEEEEHN`p|..",
    "...iyp^JBBBBBAA=AAAAAABCCGI^d{p.",
    "...vv_^GCBBBAA=AABAAAAAABGGIlcxf",
    "...vt_LGCCCAA=AABBBBAAA=AACGKlxf",
    "..yq_^JGCCB=<222:EECBBA==A21Dlyf",
    "..yq_KGCCA=<1!!!#AHHCB=<<5!!0b|f",
    "..xq_LAAA=<2!!!UUfaBB=<:6#![tb~f",
    "..yq_L=AAAA0!!!izxa:<<<:5!![ib~f",
    "..{q_LBCCCG0!!!hvva:<==<5!![vb~f",
    "..{q_KECCGH0!!!hvva:<ABE6!!hub~f",
    "..yq_KECAAC0!!!fuxa:=ABG6!!hi`~f",
    "..xq_MCA=<<0!!!)UXF=A==A6!!7;`~f",
    "..yq_^E=<688$!!!!0EA===<=0!!)a~f",
    "..itp^J==>PPP$!!(AEA===<:6(%PX|f",
    "...fv^^IGB98P922:CBBAA=<:628;uu.",
    "...[yt_`WJIDDHEB=ABABBA=<:DLc{U.",
    "..V//jqq`^KLLLJJDDDIIJJJLNcetu..",
    ".V/--RVkppdddddddddnndddddq|u...",
    "VR-''-/Yvqety~}}}}}}}}}||~{.....",
    "V-''--Ysn^LWq~------'--/}~U.....",
    "YR,--V}lMLJJd~VR,-''''/Vu~U.....",
    ".YR//YxbWLJcep;XR/---RYdp|i.....",
    "..YYYjtdcaattG<:XVVVVYmnttyi....",
    ".....h|ds{{pNGC@DFamm`ccblxx....",
    "....hieb_NFLJILNKKM^bcc_lbyy....",
    "....uvWMK216Nmnnnnddnd__beyu....",
    "....yvD22555a~|||{yxl_lbd|i.....",
    "....yuaLLNbey|....eyyncq{v......",
    "....u{scccv{u.......x{|yi.......",
    "......{~~~~i.........{y.........",
};
static const char *ART_W2[35] = {
    "...........f{|||||||{{||u.......",
    "........i{yvncccccccccccdxyi....",
    ".......tvppdWKKKLJJJJLKMbppqv...",
    "......sseMKCAAAAAAABCEGHHK^px...",
    ".....vtbMGGCA===AACCCCCCCACKcyu.",
    "....ytcWHBAAAA=AAAAABAAA=<=JWti.",
    "....~n_JEAAAAAAAAAAAA=A==<=GJnqv",
    "...tvn^IEB==ABACCCBAA==AA=AAC^px",
    "..x{l^HEECA==<000:HCAAAAAB=00Mpy",
    "..u{_MCCCCA=:(!!!#<GECA=<=(!#>p|",
    "..vy_MACCCBG0!!#vvsDEA=<:1!)zpe~",
    "..iy_MAACCEI(!!#vvsDCA=<:0!){pe~",
    "..iy_MACGGHI(!!#vss@=CA=<1!){pe~",
    "..iy_MCCCCEI(!!#vvs@<CCGG1!){pp{",
    "..i{lMGCB==B(!!#[xv@<BCGH1!)xdpx",
    "..u{l^IGA=:<(!!!#77<<=AAA2!!)1py",
    "..u|llWIE=>88$!!!!2=<==<<<(!$>py",
    "..husclLHH988P%!!2GBA==<::5$%at|",
    "...[|nlMLI:988>56CGCA=<<<:::Fpy.",
    "...YYkd__WJIIGGACHHGB==AAGIKctx.",
    "..YV/Yy{d__pxvv`NMMMLLLKMM`txi..",
    ".YV/''/Vxvssncdttsssttttsss~i...",
    ".R/'''-,V~edKL^cyYYYYYYYY~ff....",
    "VR-''--/YsWL<:M_yR-'''-/R|fp....",
    "YVR---/RtcL:65Nny[/,'',V{sxi....",
    ".YVVVVV|qbW@:aq~a@;VRRjvdKdvx...",
    "...YVVY}qllnqxx;22>;XXpcMK_qy...",
    ".......uvnlbnm>1111CINM`KMdsy...",
    "........i{c_^I52212LKcnKMcyu....",
    "........eixpbWJLNFFdydllq|u.....",
    "........i|pb^bcct{|||tpe{x......",
    "........i|c^Wlcpsxfffyxyf.......",
    "........u|nb`cp{u...............",
    ".........y||||~v................",
    "..........{yxuu.................",
};
static const char *ART_W3[35] = {
    "..........xxyyxxxxxyyy.......",
    "........qsxyyyyyyyyyxvq......",
    "......|~~dlKKKMMMMMKMn{|i....",
    "....svebbNLIGGHIIJJJJNW`ty...",
    "...vsp_KJGCA====ACEEEEGNcex..",
    "...yd^LCCAAAA===AABBBBA=Mcvx.",
    "..~dlMGA==========AAA==BL_dyu",
    ".t|l_IGA=<<<<======AA==BJ^bp{",
    "tte_MHGC=<:::<<=AABAA===CJMnx",
    "vq_MBCCCB=B0((2BECCCBA=B00Lcx",
    "vt^K<=ACEI0!!##2DHECA<<1!#1m{",
    "vt^L<===A0!!![xxaCBA=<6!!htn|",
    "vt^L=A=<<(!!![xva==A=<:!!hin|",
    "vt^K=A=<<(!!![vva<=AAA@!!hin{",
    "vt^L=AAAB0!!![sva<=ACGG!!himy",
    "xs_K=ABCE0!!!Xsva=BCCG@!![fmy",
    "xs_MHEEBB0!!!#77DCCBA=:!!#)mx",
    "ytl_MIE=88%!!!!0AAA==<:1!!8m{",
    "iiql_JE:PPP%!!0:<<===<::)PXf~",
    ".fyl_KI=988>::==<<===<<<<Nd|.",
    "..ytb^MLI:::<AEGCCCEGGGGL`yz.",
    "...|xq`W^MKLLKNMMMMMMM^bexu..",
    "...RY{sssttttttsssssstsv{i...",
    "..V///Yj~teppeykYYYYYYj|[....",
    ".V/-'-/Ysc^MK_e[/--''-R|.....",
    "VR-''-R|l_@:<W_thR''-Rj|.....",
    "R-''--~{lW<66:L`yYR/RXe|U....",
    "R,-,Rjvb_MJ:6:ceqUUUdnt~u....",
    "VR/RjtnbMWWJA:vyaII_bdtqxi...",
    ".YYjy^^cKKMMN6>@IIL_ppblsu...",
    "...k{cM`clMK0##1^^_pnl_lsu...",
    "....fvsv{ec@5226pttblM`pxu...",
    ".....iiiivtepeeessnll`cyxf...",
    ".........uuvvuuuixssssv{.....",
    ".................iuuxxx......",
};
static const char *ART_W4[36] = {
    ".........xxxxvxxyyyxu.......",
    "........tyyy{{{{{{yxvq......",
    ".....s||vl^^^^^^^^MK^vyy....",
    "....xxnncNJIIIJJIIIHJa`c|i..",
    "...vqd^NHEEBAAAAABCGGGL`qxv.",
    "..txc^JHEEGECA==ACCGGHIWle{.",
    ".tyc_LHEEEECBAA=AAACNa`K^ds.",
    ".qy_MIEEEECBBAA=====FbcWM^by",
    "vtn^JHEEECBBBA=AA==<<66:GMb{",
    "xq_MEEECBBBBAAA=====6#!!2^b~",
    "xq_LCCCCBBBBAA====<:(!!77mb|",
    "xq_LBBCCBBAAAA===<<0!!!iyeb|",
    "xq_LBCCBCCBAA====<:0!!!hxeb~",
    "xq_LBCCBBBAAAAA==<<0!!!hveb~",
    "xq_LBBCBBBABBAA===A0!!!hxe`|",
    "xqlNBBBBBBBBAAAABCC0!!!X{e`|",
    "vql_IEBBBBBBAAABBBB1!!!!)1`~",
    "uql_MHBBBBBBBBBBBBB9)!!!!9a|",
    "ffxl^J::=BCCEEECBB=88)#!8Fex",
    ".fyl_XUUUBBEEHHHGC=>>6<DNc{.",
    ".[hsUVRRRmFIIJLLJIHGCGJ`dtu.",
    "..UYR/''-VX``bbb`WWWWWbd|i..",
    ".YVR-'''-/j|{|||{{y{{|~yu...",
    ".VR-''''RV~dba`p{V////Yu....",
    "VR-'''-,RjnWLIJ`dj/-'-Yz....",
    "R-'''-,RYnlKG55IayY/-/Yz....",
    "V/,---RYql_WI22DdxmURYu.....",
    ".VVVVY}db_^dcFFesbW`p~u.....",
    ".....j|db_^`qttdnM^cdexx....",
    "......i{e`MK`bbKWMcdn`dyu...",
    ".......{y`J<<INbnnpnKLWs{...",
    "......h{qW<011>|{tlMJIt|x...",
    "......f|a61##0a~tbllpsxu....",
    ".......|dF5212m{vcexyvi.....",
    ".......uxtfffftvxv|xy.......",
    "........xyy{{yxtvxx.........",
};
static const char *ART_W5[35] = {
    "............sssvvvssvvx.........",
    "...........qy{|||yxyy|~~v.......",
    "........u||tb`````WWW``be||i....",
    ".......vxdnmNLLLLJJJJLLNamcix...",
    "......ttpWNAAACCCAAAACGEAELntf..",
    ".....ssc^JHEAAACCBAAAAABBBBN`ty.",
    "....{vb^HHGCBAAABAAAAAAABBEIWeu.",
    "....{n^ICECAAA=AAAAAAABBBCCGJ`d|",
    "...fsnMGCCCCBA===AAABBBBBCCGIMny",
    "..t~clJCCCCCAA=<5122:ECCCEGEE5;y",
    "..t|c_GCCBAAA=<6#!!#0DHGEEGG2!7}",
    "..f{c_CAAAAAAAG#!!)ivqIHCA=5!![{",
    "..f{b^BAAAAACGI!!!)vvqGGA<:5!![{",
    "..f|c^CCCCACGIJ!!!)ssq=AA=<5!![{",
    "..f|c_CCCCCCGHI!!!)vsq<=ACG:!![y",
    "..f|c_BCBCAAAAG!!!#fuq==ACG<!![y",
    "..f{c_L=BBAA=<:!!!!)7;<<==A:#!7{",
    "..f{c_KBECCCA>8%!!!!#5<<<<:6)!7|",
    "...fyd^MIHEEE9PP%!!!5GBA=<:58PX|",
    "...U{p_^MJHHH>8886:<=BECBA<66myi",
    "..VRYjdc_MLJICAGGCA==BEEGIJJWpy.",
    ".YV,-V~~b```bcKKKKLLJJJLLNWdqyi.",
    ".V/''-/Y|~|{yvtsvvssttttsstxx...",
    "V,-''-/R}qcc`nv}YYYYYYYYY|vi....",
    "VR---,Rjpc^LL`duV,------,}f.....",
    "YVR//RVyb^J56L^xeRR/''',V|f.....",
    ".YVVVYjyn`G556a|mmUR///R~h......",
    ".....hxttp`F@@qq65D[[[[xxuf.....",
    ".....yq_nqtqcWF016@M^bppc{f.....",
    ".....xeK@GM`caL26KK^lbnlcyi.....",
    ".....uqM6D^b|qccccnpdc^bpyi.....",
    ".....xypM_b~~~||yyxq_^^n{i......",
    "......u|yyy{f....iyblbsyy.......",
    "........uui.......vvss{.........",
    "...................yxxx.........",
};
static const char *ART_JUMP[35] = {
    "...............yyyyyyyyyy{{{u..........",
    "............vitqqeeeeqqqqqqqqti........",
    "...........vxvvb`KLLLLKMMMK_ntii.......",
    "..........{|cWLJJIHEEEEHIIIGGLKs{......",
    "........i|plMJIHHHEEECEEEEECCGKls{.....",
    ".......iud_LEEHHEEEECCECBCBBBCCM^x.....",
    ".......{elMEBEEEBEEEECCBBAAAAABJ^ei....",
    ".......yp_JEEEEBBBBBBCBBAAA==ACGK^s{...",
    "......yqn^JHHHEB=====AABBBA===CHG^s{...",
    "......yc_NIHEHEE<2115<ABCCBA=<00:^ty...",
    "......{c^LBBBEHI0!!!(:<=ACCGG1!))Ntx...",
    "......|n^I<==AH(!!)isq:<ABCG<!!h{`qu...",
    "......|n^I<==<=#!!)vvt@=BBBG:!!hy`qu...",
    "......{c^I<===C#!!)vvsDEEBAA6!!h{btu...",
    "..VVVY{c^H<=ACI#!!)vvsDECA=<5!!hybtx...",
    "VVRRVY|c^I=ABBE#!!)ivsDEB=<:5!![[>ty...",
    "VR--,R|c^LGEB<:#!!!)X;BBBA<:6!!!#9fx...",
    "VR'''/}n__JIH>8P!!!!!2=====<<0!!%9fu...",
    "VR'''-/|p_KJJ>PPP$!!1::<==ABEB(%Ppixi..",
    "VR-'''-~tn`MJD988956::<<=AACGGBJNsxx~..",
    "VVR-'--RjsdbMJHDE:BC=====AAEHIJ_tynnt|.",
    "..R,---,YyvncnM^MMMMKLLJJLNNWbtv{bLNc~i",
    "..VV,,,Ykd`Eappeeqqqqqeppqxxxx|yaJINn~i",
    "...YVRR}dWB55ayx}jjj}}}}}Y//R~dc`WWcq{.",
    "....YYY|lL66:`xcU//-----''--R|epppdeyu.",
    ".......|`J<L_bvWNXR,,''''',RUy||~|{yu..",
    ".......~ql^_nvcKKWmUV---,R[neu.........",
    ".......hxttttdWMKLDD;YVVYUcqt..........",
    "........iiiixcb^:205DFFW`ddih..........",
    "............|yxM20((:W^_by|f...........",
    ".............f{_622:Msxxxu.............",
    "..............{WMMM`q{iu...............",
    "..............~pl_cpyu.................",
    "..............uypdqxi..................",
    "...............y{||....................",
};
static const char *ART_FALL[33] = {
    "................sssvvvvvvx.........",
    "..............qqvvxxyxxxxxi........",
    "............||ytl^^^^^^^^lt{{i.....",
    "..........uxq``WLJJLLLJJIJF``q{....",
    ".........itdcMLHEEGEHEECAAAJKcdu...",
    ".........yp_MIHHHEGECCBBAACGIN`si..",
    "........{q^MIHHEEECCCCBAAAAACIWbvi.",
    "........s_MIEEEECCCCCBCAAAAAACGMst.",
    ".......it^LECECECCCBBBBAACAAAAAMeti",
    "......xqc^JGCECECBAAAACCCCCCCCGG^e{",
    "......ye_MIHGGGEEA==AAACECCCAACC^e{",
    "..VV..|e_MIHHHEEGGCA==ABCCECAA10Ne{",
    "VVRRRV{e^KEEGCB=1###5==ABCCB=2#)Fe|",
    "VR--/R{p^L=CCAA2!!!!!6ABCGGGH(#xne|",
    "VR-''-|e^L=ACEH!!!#Ust=CGGGGI##yney",
    "VR'''-|e_KEBABG!!!)uvs<=AAAAG##{cex",
    "VR'''-|q_MGB=<<!!!)xxv<<=A<<=##|cpx",
    "VV,''-}fn_IAA==!!!)yxv<<==<:=##kapy",
    ".YV----je_JGHH=!!!)hvv@====<=#!!Xiu",
    "..V/---Yel_KJ98%!!!)XX=AAAA==2$%hi.",
    "...YR//R{s_MK9PP%!!!!6A=<==AC>87xi.",
    "...Uj}}jUyxn^D98P%#(GG=::::<CKbyxh.",
    "...UhxncWcdspn`FmmWKJI=JNNNMWct~...",
    "...yvp_LGM_nqsesxve^KKKM_```sxx....",
    "...|e_K55K^^epdcKLWsxyyyy||{y......",
    "...{p^226^leqlKLJIJms|YR|x.........",
    "...{qcJ6Nddsq_MJI520;fjVsf.........",
    "...yxscacx{{snlKI0#!0dv~...........",
    "....x~|||i..i~ebK2112Lc|...........",
    ".............q|{c_^L:Jb{...........",
    "...............tstb_^nty...........",
    "................ixeeet{f...........",
    ".................i{{{{f............",
};
static const char *ART_HURT[35] = {
    "............xvvvvvvsssvvvvu........",
    "...........tyxxxxxyyy{{{{{yq.......",
    "........u{yscMNNNWWWWW``WWcsy{.....",
    "......uusnnnNIIJJJJJJJLLLJNacdvi...",
    ".....ivpnWJIHEEEEEEEBBBBBA=ANlsuf..",
    ".....xeWLHHEEEEEBECBAAAAAAABHMbp~..",
    "...v{q^LCBBBBBBBBBAAAA====AABE^n~..",
    "...iv^MIBBAAABBBBAAAAA========GWci.",
    "...iv^MEBA==ABBAAAA======<<<<::N`vx",
    "..ye_^JECB==:::A==A======<:255:JNvy",
    "..xe_MHHGHH2!!!1==A=====<60!!#6JKvx",
    "..xe^KEGHI5!!!!!1A====<<:0!!!!0LMvx",
    "..xe^KACHH!!!!!!!<=====<5!!!!!!2^xx",
    "..ye_N==A@!7U#!!!:<=====6!!!#h)2^yx",
    "..{q_N<:::!)UhU7!6:<===C:!UU[U#1Mxx",
    "..{q_N::::!)UUUu!6:<ABCH:!iUXU)1Nxx",
    "..{e_N<:::!77!#7!6<=BCEH<!7#!X)1Mxx",
    "..{e_L=BB>%!!!!!!:<=BBEHB#!!!!)9^yx",
    "..ye^^HH9PP$!!!!2:<=ABBEI5#!!$%9^{y",
    "..fit^MJ>PPP$!#2=<==AAABHI5#$%8;eu.",
    "...hxn_KJ988915IHEBBBAABEIJ299Fcy..",
    "...j}yb`MG<:BIJJIIIHGECCEIJJNWevu..",
    "..YVR~~q^^WWMKMMNWWWNNNKKLWldv|x...",
    "..V/-//{vvxxvvvsvvxyxxvsssssvv|f...",
    ".V/-'-Yvpddx|VVVVVVYYYYVVVY|d_qyf..",
    "VR-'-VucMW_suR,'''-----'',R{n^`d{..",
    "V---/~cMID_spDRR/-''''',RVmynKNn|..",
    "VR,-R~cMKWntmIa;X//////RUmlyp^cqy..",
    ".VVVY}xncdxdLIJLWYVVVVVU`_by{stvf..",
    "..YYYUy~|xlWLIJLLJDLLNLK^cees|ui...",
    ".......|e`^MMMWWWWMWW^`eel^_qy.....",
    ".......|pW55N`bvvssssstnc@6Mq{.....",
    ".......|tcNN^dt~uuiiixybWMLdvy.....",
    ".......u{vqqtuuy.....ixxsssyx......",
    "........ixxxxy.........xxuuu.......",
};
static const char *ART_KICK0[22] = {
    ".....LLLLJv.........................",
    "...JJLLLJJxu........................",
    ".iFJJJJJJJv{........................",
    ".tJJJJJJIIn{........................",
    "usJJJJJIIHaxd.......................",
    "uvJJJIIIII`e|.......................",
    "uvJIIIIIIJNcshfh....................",
    "uvNLIIIIIJLM`~~~z...................",
    "xx`NJIIIIJM__bb`t{{.....,,,,,,,,....",
    ".iv`JIIIIJKLLKM^`c~h......,,,,,,,...",
    "..ybLHHHIIHHEEKMKLm~.......,,---,,,.",
    "..{cNHHHIIIHHHJH12;~........,----,,.",
    "..U{dNHIIHIJJ0(((0;~........,,-&'-,,",
    "...|eN2000000(((0;uk........,,-'&'-,",
    "...h|m>(###(((01;f}.........,--'&'-,",
    "....h|f1(((()0[~~kU........,,-'&&'-,",
    ".....hu~~~~|{|kkh.........,,-'&&'-,,",
    "......U[[[[[[[77......,,,,,--&&'-,,.",
    ".....//////////////,,,,,,-'-'---,,..",
    "......,,,,,,,,,,,,,,,,------,,,,,...",
    "........,,,,,,,,,,,,,,,,,,,,,,,.....",
    "...........,,,,,,,,,,,,,,,,,........",
};
static const char *ART_KICK1[34] = {
    "........................,,.........",
    ".......................,,,.........",
    "................,.....,,,..........",
    "...............,,....,,,,..........",
    "...............,,,..,,-,,..........",
    "...............,-,,,--',,..........",
    "..............,--,,-'&',,..........",
    "..............,'&--'&$-,,..........",
    ".............,-&$''$!&-,,,,,.......",
    "........aa`aaX-$!&$!!'-'-,,........",
    ".......fllllllU$!!!!!&--,,.........",
    ".......{llllllk#!!!!$&'-,,.........",
    ".......{lllllly7!!!!$'-,,,.........",
    "......{yllllllniU!!!$&''-,,,,,,....",
    ".....f|n____lllvU$!!!!&&''--,,,,,,.",
    ".....f|_^MKMlllv[$!!!!!!!!$'--,,,,,",
    "....i|{_^KLJL_lv[$!!!!!!!&'-,,,,...",
    "...uxncbbKLJILlvU$!!!!&&'-,,,,.....",
    "..uenl__^KJIHHWiU$!!!&'-,,,........",
    ".hsn__^MJIHHGABtU$!!&'-,,..........",
    ".~n_^^KIEEI6((:iU$!!$',,...........",
    "[|F221(((((((((hU$!!!'-,,..........",
    ".|>10(#######((u[$!!!&-,,,.........",
    ".{m>0########)7uU$$!!$',,,.........",
    "..zp>(######)X}U$$-&$!',,..........",
    "...xuuuuuuuuzhU)$$-'&&&-,..........",
    "....uhhhhhhhhU7'&$-,-'&-,,.........",
    "..............,,'$-,,,--,,.........",
    "..............,,---,,,,,,,.........",
    "...............,,-,,...,,,.........",
    "................,,,,.....,.........",
    ".................,,,...............",
    "..................,,...............",
    "..................,................",
};
static const char *ART_P0[28] = {
    "...........kkkkkkk..........",
    "........~~~~~~~~~|{k........",
    ".....{||ZTTTTTTTTTT]|~}.....",
    "....}jZZSSSSQQQQQQSTZZ|}....",
    "..jk]ZSSQQQQQQQQQQQQQT]j}...",
    "..kjTSQQQQQQQQQQQQQQQSST}j..",
    ".kjZSQQQQQQQQQQQQQQQQQSS]}j.",
    ".k]SQQQQQQQQQQQQQQQQQQQST}j.",
    "}jZQQQQQQQQQQQQQQQQQQQQQT]}k",
    "{ZSQP!!$QQQQQQQQQQQ%!!%QSTj}",
    "{ZSQ!!!!%QQQQQQQQQ%!!!!%QTj}",
    "{ZS$#)#!!PQQQQQQQP!!!##!PTjk",
    "yZS!){U!!PQQQQQQQP!!!uu!PTjk",
    "yZS!){U!!PQQQQQQQP!!!uk!PTjk",
    "yZS!){U!!PQQQQQQQP!!!u}!QTjk",
    "{ZS!#))!!PQQQQQQQQ!!!))!QTjk",
    "|ZSP!!!!$PQQQQQQQQ$!!!!$QTjk",
    "|ZSQ$!!!PQQQQQQQQQP$!!$PQTjk",
    "|]TSQ%%PQQQQQQQQQQQP%%PQSZjk",
    ".k]SQQQQQQQQQQQQQQQQQQQSTkk.",
    ".}]TTSQQQQTZSSQTZQQQQQSTZ|}.",
    "..k}TTTSSSTZZZZ]ZSSSTTT]|k..",
    "...k~jTTTTTTZZZZTTTTZ]]{k...",
    "....~~}}ZZTTTZZZZZ]]]}{}....",
    "....|[[fiukkk}}}}{vi]Z]y....",
    "....|[mmptyykkkk~xpdZ]jy....",
    ".....yyyy{}......y{y{{{.....",
    ".....xu}}}........}}}}......",
};
static const char *ART_P1[28] = {
    ".........}}}}}}kkkkkk........",
    "........k||}||}}}}kkk........",
    ".....k~~]TSSSSSQQQQT]|~k.....",
    "....}|ZTSSQQQQQQQQQSSST}}....",
    "...kjZTSQQQQQQQQQQQQQQTZj~...",
    "..k}ZSSQQQQQQQQQQQQQQQQTZ}k..",
    "..~]TSQQQQQQQQQQQQQQQQQSTZ|..",
    "..~ZTQQQQQQQQQQQQQQQQQQQSZk..",
    "k{jTSQPPQQQQQQQQQQQQQPPQSTZ}k",
    "k}TSQ%!!$PQQQQQQQQQQ%!!%QSS}k",
    "k}TS%!!!!$QQQQQQQQQ%!!!!%QT|k",
    "k}TS!!))!!PQQQQQQQQ!!!))!PT~k",
    "k|TS!!i[!!PQQQQQQQQ!!!U{#PT|k",
    "k|TS!!i[!!PQQQQQQQQ!!!Uy#PT{k",
    "k|TS!!f[!!PQQQQQQQQ!!!Ux#PTyk",
    "k}TS!!!!!!PQQQQQQQQ!!!!!!PT|k",
    "k}TSP!!!!$QQQQQQQQQ%!!!!%QT|k",
    "}|TSQ%!!$PQQQQQQQQQP$!!%PST|k",
    "}|ZTSQPPPQQQQQQQQQQQP%%PQTZ|k",
    ".j|ZSSQQQQQQQQQQQQQQQQQQSZ{[.",
    "..|]TSQQQQQZZTTTZZSQQQQSZ]|..",
    "..k}jSSSSSSTTZZZZZTSSSTT]{...",
    "...[|}]TTTTTTTTTTTTTTZjk}....",
    "....~}||kZZZZZZZZZZ]]j||}....",
    "....~]]eqty{{y}}y{xttf]k|....",
    "....~j[ddex}kkkkk~iddp[}|....",
    ".....{|{y{}.......xy{{|{.....",
    ".....u}kkk.........kku}......",
};
static const char *ART_PFLAT[19] = {
    "............kk}}}}}}........",
    "...........j{y}}}|~~k.......",
    "........|~~}ZSSSSSSTj{yk....",
    "......kk]ZZTSQQQQQQSTZZ]kj..",
    ".....j}jTSQQQQQQQQQQQQSZjk..",
    "....}}]SQQQQQQQQQQQQQQQTT]{.",
    "....~]TQQQQQQQQQQQQQQQQQTZ{.",
    "...kjZSQQQQQQQQQQQQQQQQQSTZ}",
    "..j}TSQQQQQQQQQQQQQQQQQQQST~",
    "..k|TSQQQQQQQQQQQQQQQQQQQST~",
    "..k~TSQQQQQQQQQQQQQQQQQQSTT~",
    "..j~TSQQQQQQQQQQQQQQQQQSSTZ~",
    ".k||}ZQQQQQQQQQQQQQQQQQSTZjk",
    "]}]SS]]SQQQQQQQTTTTTTTSTTZ|j",
    "|]TSQZ]]QQQQQSTTT]ZSSZ]TTT]|",
    "|]TSQT]}TSSSSTT]][ZSS]kTTZX~",
    "|u]TT]x}ZZTTTTZ]{UZZT]}ZZ[{~",
    ".y{}}yyy||}}||||{yyxxxy{{||.",
    "..}}}}k.}}}}}}}}}}kkkkk}}{..",
};
static const char *ART_G0[29] = {
    ".......huuuuwwwwwuzz........",
    ".....wrwwwwwwwwwwwwwzw......",
    "....w~|r433333334444||{.....",
    "...~|O433+++++++333344o{r...",
    "..{rO4+++++++++++++++34o~w..",
    ".zwo4++++++++++++++++34Og~z.",
    ".wg4++++++++++++++++++34O{w.",
    "{rO3+++++++++++++++++++34wwr",
    "{o3+*##*+++++++++++*#!*334rz",
    "|o3*!!!!*+++++++++*!!!!*34wz",
    "~o3!!!!!#++++++++*!!!!!#+Owz",
    "~g3!)kX!!++++++++*!!!hh!*owz",
    "~g3!)|U!!++++++++*!!!uk!*Owz",
    "~o3!)|U!!++++++++*!!!}k!*Oru",
    "~o3!#7)!!++++++++*!!!77!*4ru",
    "~o3#!!!!!++++++++*!!!!!#*4rz",
    "~o3+!!!!*+++++++++*!!!!*3Owz",
    "~o3+*#(*+++++++++++*((*34Ow{",
    "|gO3+++++44+++++3++++++3Ooz{",
    ".wg4+++++gz33++4z4++++34O|z.",
    ".zroO3+++4o|zzzzO++++34Oo|..",
    "..zwoO44334ooOOO3333344ozz..",
    "..w{rgO444443344444OOOgwz...",
    "....w{groOOOOOOOOoogrr~w....",
    "....{rrrwwrrrrrrrrrzwwzz....",
    "....{oOOgwz||~~~|gggOor|....",
    "....|gO4Oor|...w~oO44ow{....",
    "....{|gooryz....~~ooow{z....",
    ".....||||{z......||{{{z.....",
};
static const char *ART_G1[30] = {
    "........{zzzzzzzzzzzz........",
    ".......rwwwwwwwrrrwwwrrw.....",
    ".....{|{o433333333334r|~.....",
    "...r~zO43+++++++++++34Og~{...",
    "...{rO43++++++++++++++3Og|z..",
    "..{gO4+++++++++++++++++3Oo|..",
    "..{o3+++++++++++++++++++3Ozg.",
    "w{r4+++++++++++++++++++++4O|w",
    "z~43+*##)+++++++++++*##)33O~z",
    "z~43*!!!!*+++++++++*!!!!)4O~z",
    "z|4+!!!!!!*++++++++!!!!!!*O|z",
    "z|4+!!![k!*++++++++!!!Uy#*4|w",
    "z|43!!![k!*++++++++!!!U{#*O~w",
    "z|O3!!!hk!*++++++++!!!U|#*O~w",
    "z{43!!!#)!*++++++++!!!))!*O~w",
    "w{43!!!!!!*++++++++!!!!!!*O|w",
    "w{43*!!!!*++++++++++!!!!*34|w",
    "z~O4+*((*+++++++++++*((*+34|z",
    "z~go3++++++++++++++++++++4O~z",
    "..~o4+++++Ow3++33wg++++34O|r.",
    "..~gO4++++3oggggrrO++334Og|..",
    "..zroO4433++ogggg4+334OOg~z..",
    "...w|ooOO44333333334OOOg|z...",
    "....g{~rgOOOOOOOOOOoow|{.....",
    ".....|zwwrrgggggggrwzzz|.....",
    ".....|gOOzzz{yyy{zwroor~.....",
    ".....|rO4Oogy...~rOO4Or|.....",
    ".....||g4Or{{...~|rO4g{|.....",
    "......||||~{.....{|{{{z......",
    ".......|{{{.......{yyy.......",
};
static const char *ART_GFLAT[17] = {
    "............w{|~~~|{{..........",
    ".........zzwrrggggggrzzz.......",
    "........wzzzgO333334Ozzz{......",
    "......w~zO4333++++++334Oz~w....",
    ".....z~o43++++++++++++33oozz...",
    ".....~o4++++++++++++++++3Orz...",
    "....zzO3+++++++++++++++++4gz{..",
    "...zwO3++++++++++++++++++3Og~..",
    "...wrO3++++++++++++++++++3Og~..",
    "...wr43++++++++++++++++++3Og~..",
    "..{|{w3+++++++++++++++++34ow~..",
    ".g~o44g4+++++++344OOOO3344grr~.",
    "zzg4++ooO3++++4OOOo344OOO433O~|",
    "|~O3+*3rg43333Oogoo++3ogo4+34g~",
    "|~zO44oyro444OOOwzO444oux44Oo{|",
    "..~{{{zyyxzzzzy{|~{{zxxyyz{{{|.",
    "..z{zzzrrzzzzzzzzzzzzzzzzz{{z{.",
};
static const char *ART_B0[27] = {
    "..........iiiiitff..........",
    "........y~~|{{yyyyyv........",
    ".....v{yncccb```aabp~|u.....",
    "....yvccMNLJJJJJJJM`ddsu....",
    "...itb^IHHHHEBBBBCEHL_dsi...",
    "..fsl^ICEEHEEBBBBBBABJ_`xf..",
    ".utc^LCBBEEEEEBBBA===<A^cxf.",
    ".ielLHBAAABEEECBA===<<<NMvf.",
    "ivdKJ65:=ABCEECBA=<:55:L^etf",
    "{dMK0!!(5ABEEEEEB=5(!!0A^_tu",
    "ydK0!!!!(AEECECCB<(!!!!0Nltv",
    "ydJ!#7)!!AECBBBAA5!!!77!>ltv",
    "ydL!)|U!!=EBAAA<<2!!!uu!>lti",
    "ydK!)|X!!:AAAAA<<2!!!uk!>lti",
    "{dN!)[X!!:AAABAAA2!!!hh!>`ti",
    "{dN!!!!!!:ABCCEEG2!!!!!!>ltv",
    "{dM2!!!!0<ACEEEHH=(!!!!0L_tv",
    "ydMG0!!06<<BEEHHHH:(!!0:L_tv",
    "uqnK=55<=A<<ACHIIHB5226JMqif",
    ".up`NLGABadFFFFaeFBAGLK^cyf.",
    ".uxeb_JIJLevssttdIHGI_lb{y..",
    "..iyec_^^^`b`l__^^^^_ld|{...",
    "...e{sdelllllllllbbbesxi....",
    "...f{ttspppppppppqstqptx....",
    "....{cllpt{{{|{y{snnccex....",
    "....xvtssvyi...pxxttssxu....",
    ".....|||yxf......y{|||x.....",
};
static const char *ART_B1[28] = {
    ".........xxxxuvvxxxx.........",
    "......uiixyyxvvvxxxvsti......",
    ".....iyve_^^^MKKMMM_pvxi.....",
    "....i~n_MMJJJIIHHHHJK__vx....",
    "...iyb^MCCEEEECB===BEJ^cqy...",
    "...id_^JECCCCCCBAAABEIM_nxi..",
    "..{q_^LCAAAABCCBBAAACJ__ld|..",
    "..|bWHAAAAAABCCBBA==GKKM^c|..",
    ".utWJ:6:<AAABBBBBA==<66EK`pit",
    "ix^KE0!!(6CAABBABCB<0!!(BM_ys",
    "ix^K0!!!!(:AABBBEHI0!!!!0L_{v",
    "iyWI!!!))!2ABBCBEIH!!!#)!2_{i",
    "ixWH!!!hu!2ACBBBCE=!!!Uy#2^yi",
    "ixW<!!!hk!2EECCCA<:!!!U{#2^xt",
    "ixW:!!!hh!5IHHECA<:!!!Uk#>_yt",
    "iyWD!!!!!!5IHHEECC<!!!!!!>l|i",
    "v|`K0!!!!(:EHEEEGHC(!!!!(Jl~i",
    "u|`M:(!!#2BCECCCEHH:(!!(5Ml|v",
    "vycNG6226HHEAA==BEIJ=215IWd{v",
    "..xbMIAGHIHmF:::=dWJJIGG^cxe.",
    "..{qd_KJHHCppmmmdeaJJL^_bt{..",
    "..iysbbMLJJneeeeel^MM^lnq{...",
    "....{{p__^^^^^__^___lbqt|....",
    "....|vsvslllbbbbblletsy{|....",
    "....|dlcdsvxy{{{{xseenbt{....",
    "....yqc`bdvviiiit|tccbnsx....",
    ".....x{{||xi.....vy{{yyx.....",
    ".....xyyy{.........xxxx......",
};
static const char *ART_BFLAT[19] = {
    ".............sssstt..........",
    "............{||{yyyyy........",
    ".........{{{db`WWWWbe|{|.....",
    "......itqpdp`MKLLKKMbdpptt...",
    "......ysplJHHHIIIIIIILMWsxq..",
    "....x{vlLHGEEHHIIIIIHGCK_n~..",
    "....xt`MJECEEHHIIIHHHGGH^l{u.",
    "...stpMJHGGEHHHIHIIHGGGGK_dsi",
    "...xd_HEHHHHHHHHHHHHHHHGG^_tv",
    "...ynMCEGHHHHHHHHHHHIIHHEM_tv",
    "...|d^CEGHHHIIIHHHHHHHHIH^_sx",
    "..q|pbCBGHHIIIIIIIHHHHIJL_bvx",
    "..~xseK=GHIIIJJJIIHGGGHL__yx.",
    "vxsD5FdKJIIIIHJ______^K^_l|v.",
    "{q`L1DnsaLJIHM^_lpnNL_d`_b`v{",
    "xqbM5Np~db_^W__bdenNNbsc_b`v{",
    "vxs``bx~tpcccccdxencne~dbbsy.",
    ".f|{|~~qxyyy||{yyxxyy{|{yyyi.",
    "...vii...isssiiitttttiii.....",
};

typedef struct {
    const char **px;      /* h rows of w characters                */
    short w, h;
    short ox, oy;         /* from the top left of the actor box     */
} Art;

enum { AF_IDLE, AF_W0, AF_W1, AF_W2, AF_W3, AF_W4, AF_W5, AF_JUMP, AF_FALL, AF_HURT, AF_KICK0, AF_KICK1, AF_P0, AF_P1, AF_PFLAT, AF_G0, AF_G1, AF_GFLAT, AF_B0, AF_B1, AF_BFLAT, AF_COUNT };

static const Art ART[AF_COUNT] = {
    { ART_IDLE,  32, 36,  -5,  -8 },
    { ART_W0,    35, 36,  -6,  -8 },
    { ART_W1,    32, 35,  -6,  -7 },
    { ART_W2,    32, 35,  -6,  -7 },
    { ART_W3,    29, 35,  -4,  -7 },
    { ART_W4,    28, 36,  -3,  -8 },
    { ART_W5,    32, 35,  -6,  -7 },
    { ART_JUMP,  39, 35,  -8,  -8 },
    { ART_FALL,  35, 33,  -6,  -8 },
    { ART_HURT,  35, 35,  -8,  -7 },
    { ART_KICK0, 36, 22,  16,   3 },
    { ART_KICK1, 35, 34,  15,  -3 },
    { ART_P0,    28, 28,  -3,   0 },
    { ART_P1,    29, 28,  -4,   0 },
    { ART_PFLAT, 28, 19,  -3,   9 },
    { ART_G0,    28, 29,  -3,  -1 },
    { ART_G1,    29, 30,  -4,  -2 },
    { ART_GFLAT, 31, 17,  -4,  11 },
    { ART_B0,    28, 27,  -3,   1 },
    { ART_B1,    29, 28,  -4,   0 },
    { ART_BFLAT, 29, 19,  -4,   9 },
};

/*  the three foes, in the order the sheet lays them out. blue comes
    last so the one foe in three that shares the player's colour is
    the rarest of them.                                            */
static const short FOE_WALK[3][2] = {
    { AF_P0, AF_P1 }, { AF_G0, AF_G1 }, { AF_B0, AF_B1 }
};
static const short FOE_FLAT[3] = { AF_PFLAT, AF_GFLAT, AF_BFLAT };

/*  a walk cycle worth of frames, in sheet order.                   */
static const short PLR_WALK[6] = {
    AF_W0, AF_W1, AF_W2, AF_W3, AF_W4, AF_W5
};

/* ------------------------------------------------------------------ */
/*  title logo                                                         */
/* ------------------------------------------------------------------ */
/*  the same treatment as the characters, off assets/src_title.png. it
    is drawn on a far chunkier grid - some thirteen screen pixels to
    the dot - so it is held at its own size and blown up by a whole
    number where it is drawn, and it keeps a palette of its own so
    its golds cannot cost the sprites any of theirs.               */
#define LOGO_W 152
#define LOGO_H 30
#define LOGO_NPAL 48
static const unsigned LOGO_PAL[LOGO_NPAL] = {
    0xF6F4F1, 0xE4D1C9, 0xFCF2B2, 0xFCED87, 0xFAE24A, 0xFBE035,
    0xFCE12C, 0xFBE768, 0xFCDA29, 0xFCCF27, 0xBBA7AE, 0xFCD512,
    0xFAC523, 0xCDB52F, 0xFBB71D, 0xF8A216, 0xFB8F12, 0xF9800E,
    0xCD7113, 0xD3C35A, 0x98832B, 0x7F7B89, 0x7AA4F3, 0xA75915,
    0x2272F9, 0xF53B7B, 0x755423, 0xB11155, 0x51445F, 0x583A21,
    0x3E2225, 0x2D4BAA, 0x282A4E, 0x281728, 0x170D28, 0x090726,
    0x0D0D34, 0x0B4EEB, 0x012EB6, 0x00279E, 0x011E82, 0x02186C,
    0x011058, 0x020B47, 0x010638, 0x01022D, 0x010225, 0x010229,
};
/*  indexed by ART_KEY, exactly as the character frames are.        */
static const char *LOGO_PX[LOGO_H] = {
    "..........F...................................................................R.........................................................................",
    ".........F0F................................................................RBAG........................................................................",
    "........B7(0B...............................................................E0-7R.......................................................................",
    ".......R=0(07R.......RRRRRRRRRRRRRR.........................................B--:R..............................SRRRRRR.................RRRRRR...........",
    "........RA1AG.......SGAAAAAAAAAAAAFR...RRRRRRRRRRR......RRRRRRRRRR..........G7-:R...RR.......RRRRRRRRRR......RSG=====BRR...RRRRRR.....REBBBBFR..........",
    ".........G=GGSSQS..QS=(&%%%%%%%)'':R..RG=6600000ARR....RR7777777=RR.....R...R71BG..S7=G....RGF0660000AFR....SS=7)%&''0=RR.RB7777GR...RR7)%'+:SR.........",
    "............RLI;LRQOO:1%!$$$$$$%'/5SRRB&)!$&'''''7GR.RRA0%!$''''*7GR..RE=F...F2G..G=-1B...RB0&$!$&'''''BRR.RG='&!!%''&'7GRR/%$)'7GR..RE&!$(3:NQ.........",
    "....RS.....SSMI;;CPPP:1&%'(((((((25RSB0$%)(((((((1=S.RA&%)%'((''('7SR.R0-:S..G5G..S02:F...F0$%))(((((((2BR.RB)$)%&((&!$/=RR2)$&'/AS..R=&)'15HKQ.........",
    "...G==G..SRLJI;;;;IMS=2(((*1111124:SG0%!%(((((((+3=RSR0$!&((((((((1AR.GB22BR.SRS.RA2:G...RB%!%((((((((14ARRR0!!&((((')%*2AR:1(((*5S.SE0&(*3=MLS..AE.....",
    "...A22=.SM;;;;;;;;;;CG:/((2444444:FQ='$%((13341/35PNS7'!%'(+///*(14=S..SE5:R.....SA:G....R0%$'(*1333/+4=OPS7&!)(((((((((/=SE5+(((/ASR=)'(15FKO..E0=R....",
    "...G==S.SKI;;;;;;;;;;LF1((2BQPPPPQLF0)&((25BEE=45ELNB0%&'(135553245PN....SS..RRSQ.RR....RR0((((15AAA54:PKPB0%'((*//+((((+5EQ=1(((*=SS&)(*3ALKQ.A0(/7G...",
    "....SR..QOI;;;;;;;;;;;Q7((2BLJJJJJLE'!)(15EMKKOEELJO=&!'(/4APOH=4:FKM......SR888@R......RR0((((2AOMMHAELJP=)!((*24431((((1BNP5+((*/E=&'(24HJN..B7*0AG...",
    "........QK;;;;;!!9;;9!C=((2BONNNMKS7')'*3AMKKKKMLKLG7')(+3ANJJKNQPKKP.....RD#!!!!8HG....RR2*(((*=EPOMLKJNF7)!(*2:QQB51(((+=GPA1(((*06'(+4AMKQ...G5BG....",
    ".......SL;;;;;!!,899!8D=((/7@@@=ESR0(((14EORRSSRRROE((((15PJMMMMMMMNS....Q@#!!!!!!,Q....QNA1(((**07=QOLLQB(((*15SLJNB2*(((3FLE5+(((&'**25PJNQ....EF..E..",
    "....SSSQJI;;;;!#QQ,!!SQ=***&)))+5SR0***2=SE6))66+0EE****3=NNEAAAAAAFS...QD!!!!!!!!#8H...SKF5/****''&6=FPOB****2BNJJJO=****3FLN=2******+4AMJP........B5=.",
    "..RSBBBFOJI;;;!#N8,!!DD=+++++++15RR0+++2AS=)$%%)*3AE++++3BLQ0$$$%'/:R...S#!,,#!,,#!#DQ...NKB52/+++++&&7GSB++++3ELJJJN=++++3FNKQ5++++++25FJNQ.RSRSS..A25E",
    "RG>><<<>FOJI;9!!;C#!!9@7+++////35SR0+++3AS=+*++++3AE++++3BLS+)&**+2:R..Q8!8DDD8MMD##,R...PKME:32/+++++/=SB++++3ELJJJN=+++/3FPKMA1++++/4BLJQSFB>>>BFQ.AB.",
    "R<<<<<>>>>BJ;##!!!/1$,Q7++244444:SR0+++3AS=221+++4AE++++3BMS/1/+++2:R..H#!H;;KC;;LD##@S..RMJMP=543/++++2BB++++3ELJJJN=+++14FMMJE5++++2:PJNS@<<<<<<<BR...",
    "RB<<>>BED@>>>8!!!,:=@D8E7+3ABBBBHNR0+++2APF=:51++3AE++++1BOO=441++2:R..D!!O;;;;;;;O##8Q...QPMKNFE:3++++2AB++++/=PJJLE7+++24FLPJM=+++/4BMKP@<<<<<<<<<BS..",
    ".RB>BQQMI;;C><<<<>ENK9!8A+3BMKKKKLR5/++/7GMNPA/++4AE3/+++=QMOQF2++2:R.R8!!O;;;I;;;N##,H..SB0BOLKLQ:/+++2AE31+++1FPMH=+++13AOMPNL=+++25ELMG#!#<<<<<<<>G..",
    ".SFFS.R,9IJJIC,<<@KIII,D=+3BLJJJJPR52++++=QOH7+++4AG=1+++/BPOPA+++2:S.S#!!O;;JSJ;;N###@.QE6$&AEPOF5+++/3AQ=1++++0AA7&++/34ELO.QP=+++2:OKP>8,!<<<<##<<ES.",
    "......S8,OOI;9!!!CPJJOSA/+2BSSQSSSSB42++++787+++14ASA41++++7==/++/3:R.R#!!O;;N8M;;N###@QF7%)++0886&+++34ARA42++++%$)*+134=OJO..S=+++2:OLR>@@!<<<,!!#<>S.",
    ".......SQQJI;!!!!HQPQB5/++/6,,,,,0BGA32+++'&'++134APO:31+++'''++/34:S.R#!!O;;N,M;;O,##@SE/1+++*&&&++/34:ONO:41+++&)*+2345ELKQ..S=+++2:OLS>88!<<<D8!#<>S.",
    "........SOJI;9#9CQPNO:3/+++++++'+2:QB5431111123445FOLE4321111113344BP.S#!!O;IN,M;IN###@QG53311111113444BLNLE432111123444AMJMQ..S=+++3:OLQ>#!!<<,D@!#<>S.",
    ".......Q8JIIII;;;LHPP:432222222224:SLB54444444445BLNJMB54444444444=NM.R,!!DLLD,DLLD##,DSME:44444444444=NJNJMB5444444444ANJKQ..SE01224:OLQF<,<<<<,#!,<>S.",
    ".......D!9IMSPJI;;#8QA444444444444=SJLP=5444445=EMJPNJME=4444444:BOKM.QD#!#88#!,8@,##@NPKLP=54444444:AOJMQNJME=444444=ENJKO...QB24444ANM.QE><<<<<#,<>S..",
    ".......H#,NQ..PK;!!DQOBBBBBBBBBBBBONLJKNEBBBBBENLJLQPLJLOEBBBBBBHLJJP.PO8###!!!!####8PNQNJKMFBBBBBBBHLJLP.QMJLNEBBBBBOLJJNQ...SHBBBBBOKO.SFEF>>>><>>F...",
    ".......RDHQ....Q,!@QPJKKKKKKKKKKKKKNPNJJKKKKKKKJJMQ..QLJJKKKKKKKKJKOQ..QQ@#########@HOP.QOKJKKKKKKKKKJKP...QOJJKKKKKKJJLOQ....QOKKKKKJLS.SB>ESSSEEEGS...",
    "........S......Q@@S.QLJJJJJJJJJJJJLP.QOMJJJJJJJMOP....POMJJJJJJJKOP.....SSD8#####8EQQQ...QOOKJJJJJJJKOP......ONJJJJJJLOP.......QMJJJJKP...RRR...SB>ES...",
    "................SQ...QSQQQQQQQQSQSS....SSSSSQQSS........QSSSSQSSSS.......SSSSSSSSSSS.......RQSQQQQQSSQ........SSSQSSQSS.........SSSQSS...........RRR....",
};


static const char *SPR_BLOCK[16] = {
    "NNNNNNNNNNNNNNNN",
    "NnnnnnnNnnnnnnnN",
    "NnnnnnnNnnnnnnnN",
    "NnnnnnnNnnnnnnnN",
    "NnnnnnnNnnnnnnnN",
    "NnnnnnnNnnnnnnnN",
    "NnnnnnnNnnnnnnnN",
    "NNNNNNNNNNNNNNNN",
    "nnnNnnnnnnnNnnnn",
    "nnnNnnnnnnnNnnnn",
    "nnnNnnnnnnnNnnnn",
    "nnnNnnnnnnnNnnnn",
    "nnnNnnnnnnnNnnnn",
    "nnnNnnnnnnnNnnnn",
    "nnnNnnnnnnnNnnnn",
    "NNNNNNNNNNNNNNNN"
};
/*  a one way platform: a plank you can jump up through and drop off
    with DOWN. only the top eight pixels are solid looking so it never
    reads as a wall.                                                  */
static const char *SPR_PLAT[16] = {
    "cccccccccccccccc",
    "cCccccCcccCccccC",
    "CCCCCCCCCCCCCCCC",
    "kkkkkkkkkkkkkkkk",
    "..C..C.....C..C.",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................"
};
static const char *SPR_LADDER[16] = {
    "..nn........nn..",
    "..nnnnnnnnnnnn..",
    "..nnnnnnnnnnnn..",
    "..nn........nn..",
    "..nn........nn..",
    "..nn........nn..",
    "..nn........nn..",
    "..nnnnnnnnnnnn..",
    "..nnnnnnnnnnnn..",
    "..nn........nn..",
    "..nn........nn..",
    "..nn........nn..",
    "..nn........nn..",
    "..nnnnnnnnnnnn..",
    "..nnnnnnnnnnnn..",
    "..nn........nn.."
};
static const char *SPR_APPLE[16] = {
    "........n.......",
    ".....gg.n.......",
    "......ggn.......",
    "...rrrr.rrrr....",
    "..rrrrrrrrrrrr..",
    ".rrwrrrrrrrrrrr.",
    "rrwwrrrrrrrrrrrr",
    "rrwrrrrrrrrrrrrr",
    "rrrrrrrrrrrrrrrr",
    "rrrrrrrrrrrrrrrr",
    "RrrrrrrrrrrrrrRR",
    ".RrrrrrrrrrrrRR.",
    ".RRrrrrrrrrRRR..",
    "..RRRrrrrRRRR...",
    "....RRRRRRR.....",
    "................"
};
static const char *SPR_CHERRY[16] = {
    "..........g.....",
    ".........g......",
    "....gggggg......",
    "...g....g.g.....",
    "..g....g...g....",
    ".g....g.....g...",
    "g....g.......g..",
    "....rrr...rrr...",
    "...rwrrr.rwrrr..",
    "..rrrrrrrrrrrrr.",
    "..rrrrrrrrrrrrr.",
    "..rrrrrrrrrrrrr.",
    "...RrrrR.RrrrR..",
    "....RRR...RRR...",
    "................",
    "................"
};
static const char *SPR_HOUSE[16] = {
    ".......rr.......",
    "......rrrr......",
    ".....rrrrrr.....",
    "....rrrrrrrr....",
    "...rrrrrrrrrr...",
    "..rrrrrrrrrrrr..",
    ".rrrrrrrrrrrrrr.",
    "rrrrrrrrrrrrrrrr",
    ".yyyyyyyyyyyyyy.",
    ".yyyyyyyyyyyyyy.",
    ".yyyyynnnnyyyyy.",
    ".yyyyynnnnyyyyy.",
    ".yyyyynnnnyyyyy.",
    ".yyyyynnwnyyyyy.",
    ".yyyyynnnnyyyyy.",
    ".EEEEEEEEEEEEEE."
};
static const char *SPR_HOUSE_OPEN[16] = {
    ".......rr.......",
    "......rrrr......",
    ".....rrrrrr.....",
    "....rrrrrrrr....",
    "...rrrrrrrrrr...",
    "..rrrrrrrrrrrr..",
    ".rrrrrrrrrrrrrr.",
    "rrrrrrrrrrrrrrrr",
    ".yyyyyyyyyyyyyy.",
    ".yyyyyyyyyyyyyy.",
    ".yyyyykkkkyyyyy.",
    ".yyyynkkkknyyyy.",
    ".yyyynkkkknyyyy.",
    ".yyyynkkkknyyyy.",
    ".yyyynkkkknyyyy.",
    ".EEEEEEEEEEEEEE."
};
static const char *SPR_SPIKE[16] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".W...W...W...W..",
    ".W...W...W...W..",
    "WWW.WWW.WWW.WWW.",
    "WWW.WWW.WWW.WWW.",
    "WWWWWWWWWWWWWWWW",
    "EEEEEEEEEEEEEEEE",
    "EEEEEEEEEEEEEEEE",
    "DDDDDDDDDDDDDDDD"
};

/* ------------------------------------------------------------------ */
/*  stages                                                             */
/* ------------------------------------------------------------------ */
/*  #  solid block      =  one way platform (jump up through it,
    H  ladder                press DOWN to drop off)
    o  fruit(apple)      Q  fruit(cherry)
    ^  spike (death)     G  goal house
    P  player start      1  enemy start                             */

/*  the world is 64 tiles wide with no side walls at all - walk off one
    end and you come back on the other, city connection style. solid
    ground is row 16 and the platforms sit at rows 12, 8 and 4, which
    makes four decks of three tiles head room each.

    a ladder always spans from one row above the platform it passes
    through down to the last walkable row of the deck below, so its
    top is somewhere you can actually step off onto.                 */

static const char *STAGES[NSTAGE][MAPH] = {
{   "                                                                ",     /* 1 - hills and a long low road */
    "                                                                ",
    "                                              =====             ",
    "     H  o            o                  o         o     H    G  ",
    "=====H======     ================      =================H=======",
    "     H                                         Q        H       ",
    "     H      ====                             =====      H       ",
    "   o H                  o H             o   1       o   H       ",
    "=====H============     ===H====================     ====H=======",
    "     H                    H       Q                     H       ",
    "     H                    H      =====                  H       ",
    " o   H       o      1     H   o            H    o       H o     ",
    "=====H=========     ======H=========    ===H================   =",
    "     H     Q              H    Q           H                    ",
    "     H    ====            H   =====        H                    ",
    "P o  H   o          o     H       o     1  H      o          o  ",
    "################################################################" },

{   "                                                                ",     /* 2 - gaps everywhere, goal up top */
    "             Q                                                  ",
    "            =====                 ====                          ",
    " o                  o         1     o H             G  o        ",
    "====       ==============     ========H=====      ==============",
    "                            Q         H                         ",
    "                          ======      H                         ",
    "  o       1       o  H             o  H          o          H o ",
    "============      ===H========     ===H===============    ==H===",
    "     Q               H                H                   Q H   ",
    "    =====            H                H                 ====H   ",
    "      o  H     o     H     o          H    o      1 H    o  H   ",
    "=========H===========H======      ====H======     ==H===========",
    "         H           H                H      Q  Q   H           ",
    "         H      ==== H                H     ======  H           ",
    " P  o    H       o   H           o  1 H        o    H     o     ",
    "################################################################" },

{   "                                                                ",     /* 3 - spikes on the ground floor */
    "                                                    Q           ",
    "                                                  ======        ",
    "         o  1           o      H         o                o   G ",
    "==============       ==========H===============        =========",
    "                  Q            H                                ",
    "                =====          H                                ",
    "     o H              o        H   o        H1     o         o  ",
    "=======H==      ===============H====      ==H===================",
    "       H     Q                 H        Q   H                   ",
    "       H    =====              H      ===== H                   ",
    "  o    H        o  H  1      o H         o  H          o H      ",
    "=======H===========H====     ===============H=====     ==H======",
    "       H           H       Q                H            H      ",
    "       H           H      =====             H            H      ",
    "P  o   H     ^^    Ho     ^^     o     ^^  1H o     ^^   H  o   ",
    "################################################################" },

{   "                                                                ",     /* 4 - staggered ladders */
    "            Q                                                   ",
    "          =====                                                 ",
    "      o             1   H  o                 o      o H G       ",
    "========      ==========H===========       ===========H===     =",
    "                        H        Q                    H      Q  ",
    "                        H       =====                 H     ====",
    " o            H  o      H o             1   H  o      H o       ",
    "==     =======H===============      ========H=============    ==",
    "              H               Q             H                   ",
    "              H             =====           H                   ",
    "    H  o      H 1   H o           H   o     H1         o    H o ",
    "====H====    =======H======     ==H=============     =======H===",
    "    H     Q         H             H                 Q       H   ",
    "    H    ====       H             H               ======    H   ",
    "P o H           o   H         o   H  1    o              o  H   ",
    "################################################################" },

{   "                                                                ",     /* 5 - three foes and two spike beds */
    "                                   Q                            ",
    "                                 =====                          ",
    "  o   H G          o              o   H     1           o       ",
    "======H===      ============     =====H=======       ===========",
    "      H                  Q            H                         ",
    "      H                 ======        H                         ",
    "      H   o           H   o   1       H   o       1   H   o     ",
    "======H========     ==H===============H=====      ====H=========",
    "      H         Q     H               H         Q     H         ",
    "      H        =====  H               H        =====  H         ",
    " o    H          o    H  1         o  H            o  H   o     ",
    "======H=====     =====H======     ====H======     ====H=====   =",
    "      H               H         Q     H               H     Q   ",
    "      H               H       ======  H               H   ===== ",
    "P  o  H         ^^^   H   o        o  H 1   ^^^     o H 1     o ",
    "################################################################" },

{   "                                                                ",     /* 6 - the long climb */
    "                                Q                               ",
    "                              ======                            ",
    "             o   1 H     o          G   o  H            o   1   ",
    "======      =======H======      ===========H====       =========",
    "                Q  H                       H            Q       ",
    "              =====H                       H           =====    ",
    "        o  H       Ho       1     oH       H 1 H  o        H o  ",
    "==     ====H==========      =======H==    =====H=====    ==H====",
    "         Q H                       H      Q    H           H    ",
    "        ===H=                      H    =====  H           H    ",
    " o H       H   H  o        H   o  1H        o  H   H       H 1o ",
    "===H==    =====H=====     =H========     ==========H===    =====",
    "   H           H       Q   H                  Q    H            ",
    "   H           H      =====H                 ===== H            ",
    "P  H   o     1 H        o  H     ^^    o       o   H1    o  ^^  ",
    "################################################################" },
};

/* ------------------------------------------------------------------ */
/*  globals                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    float x, y, vx, vy;
    int   dir;          /* 1 right, -1 left */
    int   onground;
    int   climb;
    int   climbDir;
    int   anim;
    int   alive;
    int   think;
    int   coyote;       /* grace frames after walking off an edge */
    int   jbuf;         /* remembered jump press                  */
    int   invuln;       /* mercy frames                           */
    int   kick;         /* kick animation countdown (player)      */
    int   kickCd;
    int   dash;         /* dash frames left (player)              */
    int   dashCd;
    int   dashDir;
    int   dropThru;     /* frames of falling through platforms    */
    int   kickHit;      /* bitmask: already connected this kick   */
    int   ride;         /* index of the foe being stood on, or -1 */
    int   stun;         /* dizzy frames (enemy)                   */
    int   kbT;          /* knock back frames left (enemy)         */
    int   kbDir;
    int   mode;         /* FM_PATROL / FM_CHASE                   */
    int   modeT;
    int   drop;         /* committed to walking off a ledge       */
    int   temper;       /* 0 = wanderer .. 2 = hunter             */
} Actor;

enum { FM_PATROL, FM_CHASE };

static char   gmap[MAPH][MAPW+1];
static Actor  gPlayer;
static Actor  gFoe[MAXENEMY];
static int    gFoeCount;
static int    gFruit;

/*  score popups. spawned where a fruit was taken, they drift up and
    fade out. combo chains keep their own countdown: pick the next one
    up before it runs out and the payout is multiplied again.        */
typedef struct {
    float x, y;
    int   life;
    int   score;
    float mult;         /* > 1 means it was part of a chain */
} Popup;
static Popup  gPop[MAXPOP];
static int    gCombo;       /* fruit taken so far in this chain  */
static int    gComboT;      /* frames left to keep the chain     */
static int    gScore, gHi = 20000, gLives, gLife, gStage, gLoop;
static int    gTime, gTimeTick;
static int    gGoalX, gGoalY;
static int    gPlayerSX, gPlayerSY;

enum { ST_TITLE, ST_CONFIG, ST_READY, ST_PLAY, ST_PAUSE,
       ST_DEAD, ST_CLEAR, ST_OVER, ST_ALLCLEAR };
static int   gState, gStateT;
static int   gFrame;

/*  the field is wider than the window, so everything on it is drawn
    through the camera. gCamX is the world x sitting at screen x 0 and
    is kept inside [0,WORLD_W) - the seam is handled by the wrap
    helpers rather than by clamping anything.                        */
static float gCamX;
static int   gShakeX, gDrawY0 = HUD_H;
static int   gShakeT;            /* screen shake countdown          */
static int   gFreeze;            /* hit stop: frames the field waits */
static int   gHurtT;             /* red wash after taking a hit      */

static int   gMenuSel;           /* title menu row  */
static int   gPauseSel;          /* pause menu row  */

static HWND       gWnd;
static HDC        gMemDC;
static HBITMAP    gDIB;
static unsigned  *gPix;
static HFONT      gFontBig, gFontSml;
static int        gRunning = 1;
static int        gFull = 0;

static unsigned char gKey[256], gHit[256];

/* ------------------------------------------------------------------ */
/*  audio : chiptune synth                                             */
/* ------------------------------------------------------------------ */
#define SR        44100
#define NBUF      6
#define BUFSAMP   1470          /* 33ms */
#define ROWSAMP   (SR/8)        /* 8 rows per second */

#define RST  0                  /* rest      */
#define SUS  255                /* sustain   */

typedef struct {
    const unsigned char *mel, *har, *bas, *drm;
    int len;
    int loop;
} Song;

/* --- original score, key of C ------------------------------------- */
/*     melody (64 rows = 8 sec loop)                                  */
static const unsigned char MEL_MAIN[64] = {
    76,SUS,79,SUS, 84,SUS,79,SUS,
    81,SUS,79,SUS, 77,SUS,76,SUS,
    74,SUS,77,SUS, 76,SUS,72,SUS,
    74,SUS,SUS,SUS, RST,RST,74,76,
    77,SUS,76,SUS, 74,SUS,72,SUS,
    71,SUS,74,SUS, 79,SUS,SUS,SUS,
    81,SUS,84,SUS, 83,SUS,81,SUS,
    79,SUS,SUS,SUS, SUS,RST,RST,RST
};
static const unsigned char HAR_MAIN[64] = {
    72,SUS,76,SUS, 79,SUS,76,SUS,
    76,SUS,74,SUS, 72,SUS,72,SUS,
    71,SUS,72,SUS, 71,SUS,67,SUS,
    69,SUS,SUS,SUS, RST,RST,RST,RST,
    72,SUS,72,SUS, 69,SUS,67,SUS,
    67,SUS,69,SUS, 74,SUS,SUS,SUS,
    76,SUS,79,SUS, 79,SUS,76,SUS,
    74,SUS,SUS,SUS, SUS,RST,RST,RST
};
static const unsigned char BAS_MAIN[64] = {
    48,RST,48,RST, 55,RST,48,RST,
    45,RST,45,RST, 52,RST,45,RST,
    43,RST,43,RST, 50,RST,43,RST,
    50,RST,50,RST, 45,RST,45,RST,
    41,RST,41,RST, 48,RST,41,RST,
    43,RST,43,RST, 50,RST,43,RST,
    41,RST,41,RST, 45,RST,48,RST,
    43,RST,43,RST, 55,RST,43,RST
};
static const unsigned char DRM_MAIN[64] = {
    1,3,2,3, 1,3,2,3,  1,3,2,3, 1,3,2,3,
    1,3,2,3, 1,3,2,3,  1,3,2,3, 1,3,2,2,
    1,3,2,3, 1,3,2,3,  1,3,2,3, 1,3,2,3,
    1,3,2,3, 1,3,2,3,  1,3,2,3, 1,2,2,2
};

/*  second stage theme: the same eight second frame in the relative
    minor, so it sits next to the main tune rather than fighting it.
    a-minor, a busier drum and a four note run into the last bar to
    push the tempo along without changing it.                        */
static const unsigned char MEL_ALT[64] = {
    69,SUS,72,SUS, 76,SUS,74,SUS,
    72,SUS,71,SUS, 69,SUS,SUS,SUS,
    77,SUS,76,SUS, 74,SUS,72,SUS,
    71,SUS,SUS,74, 72,SUS,71,SUS,
    69,SUS,76,SUS, 81,SUS,79,SUS,
    77,SUS,76,SUS, 74,SUS,SUS,SUS,
    74,76,77,79, 81,SUS,79,SUS,
    77,SUS,74,SUS, 69,SUS,SUS,SUS
};
static const unsigned char HAR_ALT[64] = {
    64,SUS,69,SUS, 72,SUS,71,SUS,
    67,SUS,67,SUS, 64,SUS,SUS,SUS,
    65,SUS,69,SUS, 72,SUS,69,SUS,
    68,SUS,SUS,SUS, 64,SUS,68,SUS,
    64,SUS,69,SUS, 72,SUS,76,SUS,
    72,SUS,72,SUS, 69,SUS,SUS,SUS,
    69,SUS,74,SUS, 77,SUS,74,SUS,
    68,SUS,71,SUS, 64,SUS,SUS,SUS
};
static const unsigned char BAS_ALT[64] = {
    45,RST,45,RST, 52,RST,45,RST,
    43,RST,43,RST, 50,RST,43,RST,
    41,RST,41,RST, 48,RST,41,RST,
    40,RST,40,RST, 47,RST,40,RST,
    45,RST,45,RST, 52,RST,45,RST,
    48,RST,48,RST, 55,RST,48,RST,
    50,RST,50,RST, 57,RST,50,RST,
    40,RST,40,RST, 45,RST,45,RST
};
static const unsigned char DRM_ALT[64] = {
    1,3,2,3, 1,1,2,3,  1,3,2,3, 1,1,2,3,
    1,3,2,3, 1,1,2,3,  1,3,2,3, 1,1,2,2,
    1,3,2,3, 1,1,2,3,  1,3,2,3, 1,1,2,3,
    1,3,2,3, 1,1,2,3,  1,3,2,3, 1,2,2,2
};

/*  title theme: sixteen bars that state a tune, answer it, go somewhere
    else for four bars and then come home to a held tonic. it is played
    through exactly once and the machine falls silent afterwards.      */
static const unsigned char MEL_TITLE[128] = {
    72,SUS,76,SUS, 79,SUS,76,SUS,      /*  1  */
    81,SUS,SUS,SUS, 79,SUS,SUS,SUS,    /*  2  */
    77,SUS,81,SUS, 79,SUS,76,SUS,      /*  3  */
    74,SUS,SUS,SUS, SUS,SUS,RST,RST,   /*  4  */
    72,SUS,76,SUS, 79,SUS,84,SUS,      /*  5  */
    83,SUS,SUS,SUS, 81,SUS,SUS,SUS,    /*  6  */
    79,SUS,81,SUS, 83,SUS,79,SUS,      /*  7  */
    84,SUS,SUS,SUS, SUS,SUS,RST,RST,   /*  8  */
    81,SUS,81,SUS, 83,SUS,84,SUS,      /*  9  */
    86,SUS,SUS,SUS, 83,SUS,SUS,SUS,    /* 10  */
    79,SUS,83,SUS, 86,SUS,83,SUS,      /* 11  */
    81,SUS,SUS,SUS, SUS,SUS,RST,RST,   /* 12  */
    84,SUS,83,SUS, 81,SUS,79,SUS,      /* 13  */
    77,SUS,81,SUS, 79,SUS,76,SUS,      /* 14  */
    74,SUS,76,SUS, 77,SUS,74,SUS,      /* 15  */
    72,SUS,SUS,SUS, SUS,SUS,SUS,SUS    /* 16  */
};
static const unsigned char HAR_TITLE[128] = {
    60,SUS,64,SUS, 67,SUS,64,SUS,
    65,SUS,SUS,SUS, 67,SUS,SUS,SUS,
    62,SUS,65,SUS, 64,SUS,60,SUS,
    59,SUS,SUS,SUS, SUS,SUS,RST,RST,
    60,SUS,64,SUS, 67,SUS,72,SUS,
    67,SUS,SUS,SUS, 65,SUS,SUS,SUS,
    64,SUS,65,SUS, 67,SUS,64,SUS,
    72,SUS,SUS,SUS, SUS,SUS,RST,RST,
    65,SUS,65,SUS, 67,SUS,69,SUS,
    71,SUS,SUS,SUS, 67,SUS,SUS,SUS,
    62,SUS,67,SUS, 71,SUS,67,SUS,
    69,SUS,SUS,SUS, SUS,SUS,RST,RST,
    72,SUS,71,SUS, 69,SUS,67,SUS,
    65,SUS,69,SUS, 67,SUS,64,SUS,
    62,SUS,64,SUS, 65,SUS,62,SUS,
    60,SUS,SUS,SUS, SUS,SUS,SUS,SUS
};
static const unsigned char BAS_TITLE[128] = {
    48,RST,48,RST, 55,RST,48,RST,
    41,RST,41,RST, 48,RST,41,RST,
    43,RST,43,RST, 50,RST,43,RST,
    43,RST,43,RST, 43,RST,43,RST,
    48,RST,48,RST, 55,RST,48,RST,
    41,RST,41,RST, 48,RST,41,RST,
    43,RST,43,RST, 50,RST,43,RST,
    48,RST,48,RST, 48,RST,55,RST,
    41,RST,41,RST, 48,RST,41,RST,
    43,RST,43,RST, 50,RST,43,RST,
    43,RST,43,RST, 50,RST,43,RST,
    45,RST,45,RST, 52,RST,45,RST,
    48,RST,48,RST, 55,RST,48,RST,
    41,RST,41,RST, 48,RST,41,RST,
    43,RST,43,RST, 50,RST,43,RST,
    48,RST,RST,RST, RST,RST,RST,RST
};
static const unsigned char DRM_TITLE[128] = {
    1,3,2,3, 1,3,2,3,   1,3,2,3, 1,3,2,3,
    1,3,2,3, 1,3,2,3,   1,3,2,3, 1,3,2,2,
    1,3,2,3, 1,3,2,3,   1,3,2,3, 1,3,2,3,
    1,3,2,3, 1,3,2,3,   1,3,2,3, 1,2,2,2,
    1,3,2,3, 1,3,2,3,   1,3,2,3, 1,3,2,3,
    1,3,2,3, 1,3,2,3,   1,3,2,3, 1,3,2,2,
    1,3,2,3, 1,3,2,3,   1,3,2,3, 1,3,2,3,
    1,3,2,3, 1,2,2,2,   1,0,0,0, 0,0,0,0
};

/* stage clear jingle */
static const unsigned char MEL_CLEAR[16] = {
    72,76,79,84, 83,SUS,86,SUS, 84,SUS,SUS,SUS, SUS,RST,RST,RST
};
static const unsigned char HAR_CLEAR[16] = {
    60,64,67,72, 71,SUS,74,SUS, 72,SUS,SUS,SUS, SUS,RST,RST,RST
};
static const unsigned char BAS_CLEAR[16] = {
    48,RST,48,RST, 55,RST,55,RST, 48,RST,SUS,SUS, SUS,RST,RST,RST
};
static const unsigned char DRM_CLEAR[16] = {
    1,3,1,3, 1,3,1,3, 1,2,2,2, 0,0,0,0
};

/* game over jingle */
static const unsigned char MEL_OVER[16] = {
    72,SUS,71,SUS, 69,SUS,68,SUS, 67,SUS,SUS,SUS, SUS,RST,RST,RST
};
static const unsigned char HAR_OVER[16] = {
    64,SUS,62,SUS, 60,SUS,59,SUS, 55,SUS,SUS,SUS, SUS,RST,RST,RST
};
static const unsigned char BAS_OVER[16] = {
    48,RST,47,RST, 45,RST,44,RST, 43,RST,SUS,SUS, SUS,RST,RST,RST
};
static const unsigned char DRM_OVER[16] = {
    1,0,1,0, 1,0,1,0, 1,2,2,2, 0,0,0,0
};

static const Song SONGS[] = {
    { MEL_MAIN,  HAR_MAIN,  BAS_MAIN,  DRM_MAIN,  64, 1 },   /* 0 main A  */
    { MEL_ALT,   HAR_ALT,   BAS_ALT,   DRM_ALT,   64, 1 },   /* 1 main B  */
    { MEL_TITLE, HAR_TITLE, BAS_TITLE, DRM_TITLE,128, 0 },   /* 2 title   */
    { MEL_CLEAR, HAR_CLEAR, BAS_CLEAR, DRM_CLEAR, 16, 0 },   /* 3 clear   */
    { MEL_OVER,  HAR_OVER,  BAS_OVER,  DRM_OVER,  16, 0 },   /* 4 over    */
    { NULL,      NULL,      NULL,      NULL,       1, 1 }    /* 5 silent  */
};
#define SONG_MAIN  0
#define SONG_ALT   1
#define SONG_TITLE 2
#define SONG_CLEAR 3
#define SONG_OVER  4
#define SONG_NONE  5

typedef struct {
    double phase, freq, vol, duty, decay;
    int    on;
} Pulse;

static Pulse         aMel, aHar, aBas, aSfx;
static double        aDrmVol, aDrmPhase, aDrmFreq, aDrmDecay;
static int           aDrmNoise;
static unsigned      aLfsr = 0x7f39;
static int           aSong = SONG_NONE, aSongReq = SONG_NONE, aReqNow = 0;
static int           aSongFall = SONG_NONE;   /* cue a recording took over */
static int           aRow, aRowSamp;
static int           aSfxId, aSfxT;
static int           aPaused = 0;      /* the score holds its place */
static CRITICAL_SECTION aCS;
static HWAVEOUT      aWO;
static WAVEHDR       aHdr[NBUF];
static short         aBuf[NBUF][BUFSAMP];
static HANDLE        aThread;
static volatile int  aQuit = 0;

enum { SFX_NONE, SFX_JUMP, SFX_COIN, SFX_DIE, SFX_LAND, SFX_OPEN, SFX_1UP,
       SFX_KICK, SFX_HIT, SFX_HURT, SFX_DASH, SFX_PAUSE, SFX_RESUME,
       SFX_MENU, SFX_BIND };

static double noteFreq(int n) { return 440.0 * pow(2.0, (n - 69) / 12.0); }

static void pulseNote(Pulse *p, int n, double vol, double dec)
{
    if (n == SUS) return;
    if (n == RST) { p->on = 0; return; }
    p->freq  = noteFreq(n);
    p->vol   = vol;
    p->decay = dec;
    p->on    = 1;
}

static void seqRow(void)
{
    const Song *s = &SONGS[aSong];
    if (!s->mel) return;
    if (aRow >= s->len) {
        if (s->loop) aRow = 0;
        else { aSong = SONG_NONE; aMel.on = aHar.on = aBas.on = 0; return; }
    }
    pulseNote(&aMel, s->mel[aRow], 0.26, 0.9999955);
    pulseNote(&aHar, s->har[aRow], 0.13, 0.999992);
    pulseNote(&aBas, s->bas[aRow], 0.22, 0.999988);
    if (s->drm) {
        int d = s->drm[aRow];
        if (d == 1) { aDrmVol = 0.55; aDrmDecay = 0.99980; aDrmFreq = 150; aDrmNoise = 0; aDrmPhase = 0; }
        else if (d == 2) { aDrmVol = 0.30; aDrmDecay = 0.99975; aDrmNoise = 1; }
        else if (d == 3) { aDrmVol = 0.10; aDrmDecay = 0.99900; aDrmNoise = 1; }
    }
    aRow++;
}

static void sfxTick(void)
{
    double t;
    if (!aSfxId) return;
    t = aSfxT / (double)SR;
    switch (aSfxId) {
    case SFX_JUMP:
        aSfx.freq = 330 + 900 * t; aSfx.vol = 0.20; aSfx.duty = 0.5;
        if (t > 0.13) aSfxId = 0;
        break;
    case SFX_COIN:
        aSfx.freq = (t < 0.045) ? 1046 : 1568; aSfx.vol = 0.18 * (1.0 - t * 4.0); aSfx.duty = 0.25;
        if (t > 0.22) aSfxId = 0;
        break;
    case SFX_DIE:
        aSfx.freq = 720 * exp(-2.2 * t) + 60 + 40 * sin(t * 60.0);
        aSfx.vol = 0.22 * (1.0 - t * 1.2); aSfx.duty = 0.5;
        if (t > 0.8) aSfxId = 0;
        break;
    case SFX_LAND:
        aSfx.freq = 160 - 90 * t * 8; aSfx.vol = 0.10 * (1.0 - t * 12.0); aSfx.duty = 0.5;
        if (t > 0.08) aSfxId = 0;
        break;
    case SFX_OPEN:
        aSfx.freq = 440 + 660 * sin(t * 22.0); aSfx.vol = 0.17 * (1.0 - t * 1.6); aSfx.duty = 0.25;
        if (t > 0.6) aSfxId = 0;
        break;
    case SFX_1UP:
        aSfx.freq = noteFreq(72 + (int)(t * 24) * 4); aSfx.vol = 0.18; aSfx.duty = 0.5;
        if (t > 0.5) aSfxId = 0;
        break;
    case SFX_KICK:                       /* short whoosh */
        aSfx.freq = 620 - 3200 * t; if (aSfx.freq < 150) aSfx.freq = 150;
        aSfx.vol = 0.13 * (1.0 - t * 9.0); aSfx.duty = 0.25;
        if (t > 0.11) aSfxId = 0;
        break;
    case SFX_HIT:                        /* the foe takes it  */
        aSfx.freq = 190 + 500 * exp(-14.0 * t);
        aSfx.vol = 0.22 * (1.0 - t * 4.5); aSfx.duty = 0.5;
        if (t > 0.22) aSfxId = 0;
        break;
    case SFX_HURT:                       /* lost a heart */
        aSfx.freq = 520 * exp(-4.0 * t) + 130;
        aSfx.vol = 0.20 * (1.0 - t * 2.6); aSfx.duty = 0.25;
        if (t > 0.38) aSfxId = 0;
        break;
    case SFX_DASH:                       /* rising swish */
        aSfx.freq = 260 + 2600 * t; aSfx.vol = 0.14 * (1.0 - t * 7.0); aSfx.duty = 0.12;
        if (t > 0.14) aSfxId = 0;
        break;
    /*  the pause pair: two blips falling on the way in and climbing on
        the way out, so the ear knows which one it just heard even with
        the music already cut.                                        */
    case SFX_PAUSE:
        aSfx.freq = (t < 0.075) ? 988 : 587;
        aSfx.vol  = (t > 0.066 && t < 0.078) ? 0.0 : 0.20;
        aSfx.duty = 0.5;
        if (t > 0.17) aSfxId = 0;
        break;
    case SFX_RESUME:
        aSfx.freq = (t < 0.075) ? 587 : 988;
        aSfx.vol  = (t > 0.066 && t < 0.078) ? 0.0 : 0.20;
        aSfx.duty = 0.5;
        if (t > 0.17) aSfxId = 0;
        break;
    case SFX_MENU:                       /* moving down a menu */
        aSfx.freq = 740; aSfx.vol = 0.11 * (1.0 - t * 18.0); aSfx.duty = 0.25;
        if (t > 0.05) aSfxId = 0;
        break;
    case SFX_BIND:                       /* a key was captured */
        aSfx.freq = (t < 0.05) ? 784 : 1175;
        aSfx.vol  = 0.16 * (1.0 - t * 5.0); aSfx.duty = 0.5;
        if (t > 0.19) aSfxId = 0;
        break;
    }
    if (!aSfxId) aSfx.vol = 0;
    aSfxT++;
}

static double pulseOut(Pulse *p)
{
    double v;
    if (!p->on || p->vol <= 0.0001) return 0.0;
    p->phase += p->freq / SR;
    if (p->phase >= 1.0) p->phase -= 1.0;
    v = (p->phase < p->duty) ? 1.0 : -1.0;
    p->vol *= p->decay;
    return v * p->vol;
}

/* ------------------------------------------------------------------ */
/*  streamed music                                                     */
/* ------------------------------------------------------------------ */
/*  the recorded tracks sit next to the exe and take a cue over only
    when the file for it is really there, so a bare exe still plays
    the chiptune score exactly as it always did.

    media foundation decodes the mp3 into the mixer's own format -
    mono, 44100, 16 bit - and audioRender mixes the result alongside
    the sfx. that is what makes the loop seam sample exact, and it
    lets the music toggle and the pause work on a recording without a
    line of extra plumbing. mfplat is loaded by hand rather than
    linked, so a machine without it (windows N with no media feature
    pack) still starts the game and simply hears the synth.

    a four minute track takes about a second to decode, far too long
    to spend on a stage change, so a worker thread does it and the cue
    comes in the moment the samples are there.                       */

/*  levelling. the sfx and the jingles were balanced against the synth
    score, so a recording has to arrive at that same loudness or the
    balance goes with it - and measured, the tracks came in some 7dB
    under it.

    a square wave sits far closer to its own peak than real music does,
    so the recordings cannot simply be lifted the whole way: at matching
    loudness their peaks would run past the clamp and break up. instead
    each track is measured as it is decoded and levelled to MUSIC_RMS,
    never gained past MUSIC_PEAK, and the synth side comes down by
    SYNTH_GAIN to meet it. the two then sit level, and the master is
    opened up by as much as that frees so nothing ends up quieter.   */
#define MUSIC_RMS   0.200      /* loudness both sides are held to       */
#define MUSIC_PEAK  1.250      /* a track is never gained past this     */
#define SYNTH_GAIN  0.567      /* score and sfx, down to meet it        */
#define MASTER      0.680      /* was 0.55, before the headroom opened  */
#define MUSIC_MAXS  (SR * 600) /* ten minutes; anything longer is junk  */

typedef HRESULT (WINAPI *PFN_MFStartup)(ULONG, DWORD);
typedef HRESULT (WINAPI *PFN_MFShutdown)(void);
typedef HRESULT (WINAPI *PFN_MFCreateMediaType)(IMFMediaType **);
typedef HRESULT (WINAPI *PFN_MFReaderURL)(LPCWSTR, IMFAttributes *,
                                          IMFSourceReader **);

static PFN_MFStartup          pMFStartup;
static PFN_MFShutdown         pMFShutdown;
static PFN_MFCreateMediaType  pMFCreateMediaType;
static PFN_MFReaderURL        pMFReaderURL;

/*  the handful of guids the decode needs, spelled out here so the
    build line does not have to pull in mfuuid.                      */
static const GUID MG_MAJOR = {0x48eba18e,0xf8c9,0x4687,
    {0xbf,0x11,0x0a,0x74,0xc9,0xf9,0x6a,0x8f}};
static const GUID MG_SUBTYPE = {0xf7e34c9a,0x42e8,0x4714,
    {0xb7,0x4b,0xcb,0x29,0xd7,0x2c,0x35,0xe5}};
static const GUID MG_AUDIO = {0x73647561,0x0000,0x0010,
    {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
static const GUID MG_PCM = {0x00000001,0x0000,0x0010,
    {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
static const GUID MG_BITS = {0xf2deb57f,0x40fa,0x4764,
    {0xaa,0x33,0xed,0x4f,0x2d,0x1f,0xf6,0x69}};
static const GUID MG_RATE = {0x5faeeae7,0x0290,0x4c31,
    {0x9e,0x8a,0xc5,0x34,0xf6,0x8d,0x9d,0xba}};
static const GUID MG_CHANS = {0x37e48bf5,0x645e,0x4c5b,
    {0x89,0xde,0xad,0xa9,0xe2,0x9b,0x69,0x6a}};

static char    mDir[MAX_PATH];      /* where the exe lives, with a slash */
#define SOUND_DIR "sounds\\"    /* the tracks live beside the exe   */
static int     mHaveMF = 0;         /* media foundation came up ok       */

/*  mPcm, mLen and mPos belong to the mixer and are only ever touched
    inside aCS. mGen is the generation of the request: the loader drops
    what it decoded if the cue moved on while it was working.         */
static short  *mPcm;
static int     mLen, mPos;
static double  mGain = 1.0;         /* what levelling made of the track */
static char    mCur[64];            /* track loaded, or on its way       */
static LONG    mGen;
static HANDLE  mThread;
/*  a stage change can leave an earlier decode still running. it will
    throw its work away when it sees the generation has moved, but it
    still has to take aCS to find that out, so the shutdown waits for
    every last one of them before the section goes away.            */
static volatile LONG mBusy;

static void musicInit(void)
{
    HMODULE plat, rw;
    char *p;
    GetModuleFileNameA(NULL, mDir, MAX_PATH);
    p = strrchr(mDir, 0x5C);
    if (p) p[1] = 0; else mDir[0] = 0;

    plat = LoadLibraryA("mfplat.dll");
    rw   = LoadLibraryA("mfreadwrite.dll");
    if (!plat || !rw) return;
    pMFStartup         = (PFN_MFStartup)        (void *)GetProcAddress(plat, "MFStartup");
    pMFShutdown        = (PFN_MFShutdown)       (void *)GetProcAddress(plat, "MFShutdown");
    pMFCreateMediaType = (PFN_MFCreateMediaType)(void *)GetProcAddress(plat, "MFCreateMediaType");
    pMFReaderURL       = (PFN_MFReaderURL)      (void *)GetProcAddress(rw, "MFCreateSourceReaderFromURL");
    if (!pMFStartup || !pMFShutdown || !pMFCreateMediaType || !pMFReaderURL) return;
    if (FAILED(pMFStartup(MF_VERSION, MFSTARTUP_LITE))) return;
    mHaveMF = 1;
}

/*  a track name is only ever a bare file name; this is the one place
    that knows which folder it comes out of.                          */
static void musicPath(char *out, const char *file)
{
    sprintf(out, "%s%s%s", mDir, SOUND_DIR, file);
}

/*  decode one file all the way through. returns NULL if anything at
    all went wrong - every failure lands back on the synth, which is
    always a safe place to land.                                     */
static short *musicDecode(const char *file, int *outLen)
{
    wchar_t wide[MAX_PATH + 64];
    char path[MAX_PATH + 64];
    IMFSourceReader *rd = NULL;
    IMFMediaType *mt = NULL;
    short *pcm = NULL;
    int cap = SR * 30, len = 0;

    musicPath(path, file);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) { *outLen = 0; return NULL; }
    MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH + 64);

    if (FAILED(pMFReaderURL(wide, NULL, &rd))) { *outLen = 0; return NULL; }
    if (FAILED(pMFCreateMediaType(&mt))) goto done;
    IMFMediaType_SetGUID(mt, &MG_MAJOR, &MG_AUDIO);
    IMFMediaType_SetGUID(mt, &MG_SUBTYPE, &MG_PCM);
    IMFMediaType_SetUINT32(mt, &MG_BITS, 16);
    IMFMediaType_SetUINT32(mt, &MG_RATE, SR);
    IMFMediaType_SetUINT32(mt, &MG_CHANS, 1);
    if (FAILED(IMFSourceReader_SetCurrentMediaType(
            rd, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, mt))) goto done;

    pcm = (short *)malloc((size_t)cap * sizeof(short));
    if (!pcm) goto done;
    for (;;) {
        DWORD flags = 0, cb = 0;
        IMFSample *smp = NULL;
        IMFMediaBuffer *buf = NULL;
        BYTE *raw = NULL;
        int n;
        if (FAILED(IMFSourceReader_ReadSample(rd, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                              0, NULL, &flags, NULL, &smp))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (smp) IMFSample_Release(smp);
            break;
        }
        if (!smp) continue;
        if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(smp, &buf))) {
            if (SUCCEEDED(IMFMediaBuffer_Lock(buf, &raw, NULL, &cb))) {
                n = (int)(cb / sizeof(short));
                if (len + n > cap) {
                    short *big;
                    while (len + n > cap && cap < MUSIC_MAXS) cap *= 2;
                    if (cap > MUSIC_MAXS) cap = MUSIC_MAXS;
                    if (len + n > cap) n = cap - len;      /* hard stop */
                    big = (short *)realloc(pcm, (size_t)cap * sizeof(short));
                    if (!big) {
                        IMFMediaBuffer_Unlock(buf);
                        IMFMediaBuffer_Release(buf);
                        IMFSample_Release(smp);
                        goto done;
                    }
                    pcm = big;
                }
                if (n > 0) { memcpy(pcm + len, raw, (size_t)n * sizeof(short)); len += n; }
                IMFMediaBuffer_Unlock(buf);
            }
            IMFMediaBuffer_Release(buf);
        }
        IMFSample_Release(smp);
        if (len >= MUSIC_MAXS) break;
    }
done:
    if (mt) IMFMediaType_Release(mt);
    if (rd) IMFSourceReader_Release(rd);
    if (pcm && len <= 0) { free(pcm); pcm = NULL; }
    *outLen = len;
    return pcm;
}

/*  what one track has to be multiplied by to sit at MUSIC_RMS. the rms
    is taken over the samples that are actually sounding, so a track
    that breathes is not read as a quiet one, and the result is held
    back if it would push the peak past the clamp.                    */
static double musicGain(const short *pcm, int len)
{
    double sum = 0.0, rms, g;
    int i, nz = 0, pk = 0;
    for (i = 0; i < len; i++) {
        int v = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (v > pk) pk = v;
        if (v > 300) { sum += (double)pcm[i] * pcm[i]; nz++; }
    }
    if (nz < SR || pk <= 0) return 1.0;          /* nothing to go on */
    rms = sqrt(sum / nz) / 32768.0;
    g = MUSIC_RMS / rms;
    if (g * (pk / 32768.0) > MUSIC_PEAK) g = MUSIC_PEAK / (pk / 32768.0);
    if (g > 8.0) g = 8.0;
    if (g < 0.1) g = 0.1;
    return g;
}

static DWORD WINAPI musicLoader(LPVOID arg)
{
    LONG gen = (LONG)(LONG_PTR)arg;
    char file[64];
    short *pcm;
    double gain;
    int len = 0;

    EnterCriticalSection(&aCS);
    strcpy(file, mCur);
    LeaveCriticalSection(&aCS);

    pcm = musicDecode(file, &len);
    gain = pcm ? musicGain(pcm, len) : 1.0;

    EnterCriticalSection(&aCS);
    if (gen != mGen) {                  /* the cue moved on without us */
        LeaveCriticalSection(&aCS);
        free(pcm);
        InterlockedDecrement(&mBusy);
        return 0;
    }
    free(mPcm);
    mPcm = pcm; mLen = len; mPos = 0; mGain = gain;
    if (!pcm) {                         /* no good - hand the cue back */
        mCur[0] = 0;
        aSongReq = aSongFall; aReqNow = 1;
    }
    LeaveCriticalSection(&aCS);
    InterlockedDecrement(&mBusy);
    return 0;
}

/*  drop whatever is playing and orphan any loader still working.     */
static void musicStop(void)
{
    EnterCriticalSection(&aCS);
    mGen++;
    free(mPcm); mPcm = NULL; mLen = 0; mPos = 0; mCur[0] = 0;
    LeaveCriticalSection(&aCS);
}

/*  ask for a track. returns 1 when the recording has the cue - the
    file is there and starts as soon as it is decoded - and 0 when the
    synth has to keep it.                                             */
static int musicPlay(const char *file)
{
    char path[MAX_PATH + 64];
    LONG gen;
    if (!mHaveMF || !file) { musicStop(); return 0; }

    EnterCriticalSection(&aCS);
    if (strcmp(mCur, file) == 0) { LeaveCriticalSection(&aCS); return 1; }
    LeaveCriticalSection(&aCS);

    musicPath(path, file);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) { musicStop(); return 0; }

    EnterCriticalSection(&aCS);
    gen = ++mGen;
    free(mPcm); mPcm = NULL; mLen = 0; mPos = 0;
    strncpy(mCur, file, sizeof(mCur) - 1); mCur[sizeof(mCur) - 1] = 0;
    LeaveCriticalSection(&aCS);

    if (mThread) { CloseHandle(mThread); mThread = NULL; }
    InterlockedIncrement(&mBusy);
    mThread = CreateThread(NULL, 0, musicLoader, (LPVOID)(LONG_PTR)gen, 0, NULL);
    if (!mThread) { InterlockedDecrement(&mBusy); musicStop(); return 0; }
    return 1;
}

static void musicShutdown(void)
{
    int spin;
    musicStop();                        /* every loader is now stale */
    if (mThread) { CloseHandle(mThread); mThread = NULL; }
    for (spin = 0; mBusy > 0 && spin < 800; spin++) Sleep(5);
    free(mPcm); mPcm = NULL; mLen = 0;
    if (mHaveMF) { pMFShutdown(); mHaveMF = 0; }
}

/*  which recording owns a cue. the two field tracks hang off the same
    cues the synth score already alternates by stage parity, so odd and
    even stages stay in step whichever of the two is playing. the clear
    and game over stings stay synth jingles, because those have to end
    on the same frame the state does.                                */
static const char *songTrack(int id)
{
    switch (id) {
    case SONG_TITLE: return "piko1.mp3";
    case SONG_MAIN:  return "neon1.mp3";   /* stage 1, 3, 5 .. */
    case SONG_ALT:   return "neon2.mp3";   /* stage 2, 4, 6 .. */
    }
    return NULL;
}

static void audioRender(short *out, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        /*  the synth adds up on its own before it joins the mix: the
            score and the sfx keep the balance they were written with,
            and SYNTH_GAIN moves the pair of them against a recording
            as one.                                                   */
        double m = 0.0, syn = 0.0;
        /*  a pause stops the sequencer where it stands instead of
            silencing the mixer, so the tune picks up on the same row
            when the game starts again. the sfx channel keeps running
            underneath it - that is what plays the pause blip.       */
        if (!aPaused) {
            if (aRowSamp <= 0) { seqRow(); aRowSamp += ROWSAMP; }
            aRowSamp--;

            syn += pulseOut(&aMel);
            syn += pulseOut(&aHar);
            syn += pulseOut(&aBas);

            /* drums */
            if (aDrmVol > 0.0005) {
                double d;
                if (aDrmNoise) {
                    unsigned bit = ((aLfsr ^ (aLfsr >> 1)) & 1);
                    aLfsr = (aLfsr >> 1) | (bit << 14);
                    d = (aLfsr & 1) ? 1.0 : -1.0;
                } else {
                    aDrmPhase += aDrmFreq / SR;
                    if (aDrmPhase >= 1.0) aDrmPhase -= 1.0;
                    aDrmFreq *= 0.99975;
                    if (aDrmFreq < 42) aDrmFreq = 42;
                    d = sin(aDrmPhase * 6.283185307);
                }
                syn += d * aDrmVol;
                aDrmVol *= aDrmDecay;
            }
        }

        /* sfx */
        sfxTick();
        if (aSfx.vol > 0.0001) {
            aSfx.phase += aSfx.freq / SR;
            if (aSfx.phase >= 1.0) aSfx.phase -= 1.0;
            syn += ((aSfx.phase < aSfx.duty) ? 1.0 : -1.0) * aSfx.vol;
        }

        m = syn * SYNTH_GAIN;

        /*  a recording, once decoded, plays in place of the sequencer
            - which is sitting on SONG_NONE for as long as it holds the
            cue. it comes in at the gain levelling gave it, so it meets
            the sfx where the synth score used to. looping is just a
            wrap of the play head, so the seam falls exactly on a
            sample boundary.                                          */
        if (!aPaused && mPcm && mLen > 0) {
            m += mPcm[mPos] * (mGain / 32768.0);
            if (++mPos >= mLen) mPos = 0;
        }

        m *= MASTER;
        if (m > 1.0) m = 1.0;
        if (m < -1.0) m = -1.0;
        out[i] = (short)(m * 30000.0);
    }
}

static DWORD WINAPI audioThread(LPVOID p)
{
    int i;
    (void)p;
    while (!aQuit) {
        for (i = 0; i < NBUF; i++) {
            if (aHdr[i].dwFlags & WHDR_DONE) {
                EnterCriticalSection(&aCS);
                if (aReqNow) { aSong = aSongReq; aRow = 0; aRowSamp = 0; aReqNow = 0; }
                audioRender(aBuf[i], BUFSAMP);
                LeaveCriticalSection(&aCS);
                aHdr[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(aWO, &aHdr[i], sizeof(WAVEHDR));
            }
        }
        Sleep(3);
    }
    return 0;
}

static void audioInit(void)
{
    WAVEFORMATEX wf;
    int i;
    musicInit();
    InitializeCriticalSection(&aCS);
    aMel.duty = 0.5;  aHar.duty = 0.25; aBas.duty = 0.5; aSfx.duty = 0.5;
    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 1;
    wf.nSamplesPerSec  = SR;
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = 2;
    wf.nAvgBytesPerSec = SR * 2;
    if (waveOutOpen(&aWO, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        aWO = NULL;
        return;
    }
    for (i = 0; i < NBUF; i++) {
        memset(&aHdr[i], 0, sizeof(WAVEHDR));
        aHdr[i].lpData         = (LPSTR)aBuf[i];
        aHdr[i].dwBufferLength = BUFSAMP * 2;
        waveOutPrepareHeader(aWO, &aHdr[i], sizeof(WAVEHDR));
        aHdr[i].dwFlags |= WHDR_DONE;
    }
    aThread = CreateThread(NULL, 0, audioThread, NULL, 0, NULL);
    if (aThread) SetThreadPriority(aThread, THREAD_PRIORITY_ABOVE_NORMAL);
}

static void audioShutdown(void)
{
    int i;
    musicShutdown();
    if (!aWO) return;
    aQuit = 1;
    if (aThread) { WaitForSingleObject(aThread, 500); CloseHandle(aThread); }
    waveOutReset(aWO);
    for (i = 0; i < NBUF; i++) waveOutUnprepareHeader(aWO, &aHdr[i], sizeof(WAVEHDR));
    waveOutClose(aWO);
    DeleteCriticalSection(&aCS);
}

/*  one door for every cue: the recording gets first refusal and the
    synth picks up whatever it turns down.                           */
static void playSong(int id)
{
    int rec = musicPlay(songTrack(id));
    if (!aWO) return;
    EnterCriticalSection(&aCS);
    aSongFall = id;
    aSongReq = rec ? SONG_NONE : id; aReqNow = 1;
    LeaveCriticalSection(&aCS);
}

/*  in-game bgm alternates between the two loops by stage number, so
    stage 1, 3, 5 .. get the major theme and stage 2, 4, 6 .. the minor
    one. gStage is zero based, hence the inverted test.               */
static int stageSong(void)
{
    return (gStage & 1) ? SONG_ALT : SONG_MAIN;
}

static void playSfx(int id)
{
    if (!aWO) return;
    EnterCriticalSection(&aCS);
    aSfxId = id; aSfxT = 0;
    LeaveCriticalSection(&aCS);
}

/*  hold or release the score. the notes still ringing are cut so the
    last chord does not hang over a silent pause screen.            */
static void audioPause(int on)
{
    if (!aWO) { aPaused = on; return; }
    EnterCriticalSection(&aCS);
    aPaused = on;
    if (on) {
        aMel.on = aHar.on = aBas.on = 0;
        aDrmVol = 0.0;
    }
    LeaveCriticalSection(&aCS);
}

/* ------------------------------------------------------------------ */
/*  game pad                                                           */
/* ------------------------------------------------------------------ */
/*  XInput is loaded dynamically (no import library needed) and any
    other controller is picked up through the winmm joystick API.      */

typedef struct {
    WORD  wButtons;
    BYTE  bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} XPAD_GAMEPAD;
typedef struct {
    DWORD dwPacketNumber;
    XPAD_GAMEPAD Gamepad;
} XPAD_STATE;
typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XPAD_STATE *);

#define XB_UP     0x0001
#define XB_DOWN   0x0002
#define XB_LEFT   0x0004
#define XB_RIGHT  0x0008
#define XB_START  0x0010
#define XB_BACK   0x0020
#define XB_LB     0x0100
#define XB_RB     0x0200
#define XB_A      0x1000
#define XB_B      0x2000
#define XB_X      0x4000
#define XB_Y      0x8000

/*  the pad is read as raw buttons and the bindings sit on top, so the
    key config screen can move an action anywhere without the reader
    knowing what any of them are for.                                */
#define PDIR_L    0x1
#define PDIR_R    0x2
#define PDIR_U    0x4
#define PDIR_D    0x8

enum { PBTN_NONE, PBTN_A, PBTN_B, PBTN_X, PBTN_Y,
       PBTN_L, PBTN_R, PBTN_START, PBTN_BACK, PBTN_COUNT };
#define PBTN_FULL PBTN_COUNT             /* both triggers, never bindable */

static const char *PBTN_NAME[PBTN_COUNT] =
    { "--", "A", "B", "X", "Y", "L", "R", "START", "BACK" };

#define PAD_NONE   0
#define PAD_XINPUT 1
#define PAD_JOY    2

static HMODULE          gXiDll;
static XInputGetState_t gXiGetState;
static int              gPadType = PAD_NONE;
static int              gPadIdx  = -1;
static int              gPadScan = 0;
static JOYCAPSA         gJoyCaps;
static int              gPadNotice;      /* frames left on the "pad found" note */
static unsigned gPadDir, gPadDirEdge, gPadBtn, gPadBtnEdge;
static unsigned gPadPrevDir, gPadPrevBtn;

/* ---- actions, and what is currently bound to them ------------------ */
enum { ACT_LEFT, ACT_RIGHT, ACT_UP, ACT_DOWN,
       ACT_JUMP, ACT_KICK, ACT_DASH, ACT_PAUSE, ACT_COUNT };

static const char *ACT_LABEL[ACT_COUNT] = {
    "ひだり", "みぎ", "うえ", "した",
    "ジャンプ", "キック", "ダッシュ", "ポーズ"
};

/*  two keys each, plus one pad button. the four directions always
    answer to the stick and the d-pad as well, so their pad column is
    left empty unless the player puts something else there.          */
static unsigned char gKeyBind[ACT_COUNT][2];
static unsigned char gPadBind[ACT_COUNT];

static void bindDefaults(void)
{
    static const unsigned char kb[ACT_COUNT][2] = {
        { VK_LEFT, 'A' }, { VK_RIGHT, 'D' }, { VK_UP, 'W' }, { VK_DOWN, 'S' },
        { VK_SPACE, 'Z' }, { 'X', 'K' }, { 'C', VK_SHIFT }, { VK_RETURN, 'P' }
    };
    /*  dash lands on X. it used to be the shoulders, which are the two
        buttons you cannot hit in a hurry.                           */
    static const unsigned char pb[ACT_COUNT] = {
        PBTN_NONE, PBTN_NONE, PBTN_NONE, PBTN_NONE,
        PBTN_A, PBTN_B, PBTN_X, PBTN_START
    };
    memcpy(gKeyBind, kb, sizeof(gKeyBind));
    memcpy(gPadBind, pb, sizeof(gPadBind));
}

static int padBtnBound(int btn)
{
    int i;
    for (i = 0; i < ACT_COUNT; i++) if (gPadBind[i] == btn) return 1;
    return 0;
}

static int actDown(int a)
{
    unsigned char k0 = gKeyBind[a][0], k1 = gKeyBind[a][1];
    if (k0 && gKey[k0]) return 1;
    if (k1 && gKey[k1]) return 1;
    if (a <= ACT_DOWN && (gPadDir & (1u << a))) return 1;   /* PDIR_* line up */
    if (gPadBind[a] && (gPadBtn & (1u << gPadBind[a]))) return 1;
    return 0;
}

static int actHit(int a)
{
    unsigned char k0 = gKeyBind[a][0], k1 = gKeyBind[a][1];
    if (k0 && gHit[k0]) return 1;
    if (k1 && gHit[k1]) return 1;
    if (a <= ACT_DOWN && (gPadDirEdge & (1u << a))) return 1;
    if (gPadBind[a] && (gPadBtnEdge & (1u << gPadBind[a]))) return 1;
    return 0;
}

/*  the label shown in the config table. everything printable comes out
    as itself, the rest gets a name.

    the made up names cycle through four buffers because two of these
    calls routinely end up as arguments to the same sprintf, and one
    shared buffer made a row of X and K print as X twice.           */
static const char *keyName(int vk)
{
    static char buf[4][16];
    static int  turn = 0;
    char *b;
    switch (vk) {
    case 0:             return "--";
    case VK_LEFT:       return "←";
    case VK_RIGHT:      return "→";
    case VK_UP:         return "↑";
    case VK_DOWN:       return "↓";
    case VK_SPACE:      return "SPACE";
    case VK_RETURN:     return "ENTER";
    case VK_SHIFT:      return "SHIFT";
    case VK_CONTROL:    return "CTRL";
    case VK_MENU:       return "ALT";
    case VK_TAB:        return "TAB";
    case VK_BACK:       return "BS";
    case VK_OEM_COMMA:  return ",";
    case VK_OEM_PERIOD: return ".";
    case VK_OEM_1:      return ";";
    case VK_OEM_2:      return "/";
    }
    b = buf[turn++ & 3];
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        b[0] = (char)vk; b[1] = 0;
        return b;
    }
    sprintf(b, "#%d", vk);
    return b;
}

static void doBack(void);
static void toggleFullscreen(void);

static void padLoadXi(void)
{
    static const char *dlls[] = { "xinput1_4.dll", "xinput1_3.dll",
                                  "xinput9_1_0.dll", "xinput1_2.dll", "xinput1_1.dll" };
    int i;
    for (i = 0; i < 5 && !gXiDll; i++) {
        gXiDll = LoadLibraryA(dlls[i]);
        if (gXiDll) {
            gXiGetState = (XInputGetState_t)GetProcAddress(gXiDll, "XInputGetState");
            if (!gXiGetState) { FreeLibrary(gXiDll); gXiDll = NULL; }
        }
    }
}

static void padInit(void)
{
    padLoadXi();
    gPadScan = 0;
}

static void padShutdown(void)
{
    if (gXiDll) FreeLibrary(gXiDll);
    gXiDll = NULL;
    gXiGetState = NULL;
}

/*  Look for a controller. This runs on a timer even while one is already
    in use, because the machine can hand us a dead virtual joystick slot
    at start up; without a re-scan the real pad plugged in afterwards
    would never be picked up. XInput always wins over the winmm path.
    A scan costs about 2 microseconds, so the timer can be short.        */
static void padDetect(void)
{
    int i;
    int newType = PAD_NONE, newIdx = -1;
    JOYCAPSA caps;

    if (!gXiGetState) padLoadXi();          /* in case the dll appeared later */

    if (gXiGetState) {
        XPAD_STATE st;
        for (i = 0; i < 4; i++) {
            if (gXiGetState((DWORD)i, &st) == ERROR_SUCCESS) {
                newType = PAD_XINPUT;
                newIdx  = i;
                break;
            }
        }
    }

    if (newType == PAD_NONE && joyGetNumDevs() > 0) {
        JOYINFOEX ji;
        memset(&ji, 0, sizeof(ji));
        ji.dwSize  = sizeof(ji);
        ji.dwFlags = JOY_RETURNALL;
        for (i = JOYSTICKID1; i <= JOYSTICKID1 + 3; i++) {
            if (joyGetPosEx((UINT)i, &ji) != JOYERR_NOERROR) continue;
            if (joyGetDevCapsA((UINT_PTR)i, &caps, sizeof(caps)) != JOYERR_NOERROR) continue;
            /*  a phantom slot reports no buttons, or sits jammed in the
                bottom left corner instead of centred                     */
            if (caps.wNumButtons == 0) continue;
            if (caps.wXmax <= caps.wXmin || caps.wYmax <= caps.wYmin) continue;
            if (ji.dwXpos == caps.wXmin && ji.dwYpos == caps.wYmin) continue;
            gJoyCaps = caps;
            newType  = PAD_JOY;
            newIdx   = i;
            break;
        }
    }

    if (newType != gPadType || newIdx != gPadIdx) {
        gPadPrevDir = gPadPrevBtn = 0;      /* no phantom press on a swap */
        if (newType != PAD_NONE) gPadNotice = 150;
    }
    gPadType = newType;
    gPadIdx  = newIdx;
}

static void padRead(unsigned *odir, unsigned *obtn)
{
    unsigned d = 0, b = 0;

    if (gPadType == PAD_XINPUT) {
        XPAD_STATE st;
        SHORT lx, ly;
        if (gXiGetState((DWORD)gPadIdx, &st) != ERROR_SUCCESS) { gPadType = PAD_NONE; return; }
        if (st.Gamepad.wButtons & XB_LEFT)  d |= PDIR_L;
        if (st.Gamepad.wButtons & XB_RIGHT) d |= PDIR_R;
        if (st.Gamepad.wButtons & XB_UP)    d |= PDIR_U;
        if (st.Gamepad.wButtons & XB_DOWN)  d |= PDIR_D;
        lx = st.Gamepad.sThumbLX;
        ly = st.Gamepad.sThumbLY;
        if (lx < -10000) d |= PDIR_L;
        if (lx >  10000) d |= PDIR_R;
        if (ly >  10000) d |= PDIR_U;
        if (ly < -10000) d |= PDIR_D;
        if (st.Gamepad.wButtons & XB_A)     b |= 1u << PBTN_A;
        if (st.Gamepad.wButtons & XB_B)     b |= 1u << PBTN_B;
        if (st.Gamepad.wButtons & XB_X)     b |= 1u << PBTN_X;
        if (st.Gamepad.wButtons & XB_Y)     b |= 1u << PBTN_Y;
        if (st.Gamepad.wButtons & XB_LB)    b |= 1u << PBTN_L;
        if (st.Gamepad.wButtons & XB_RB)    b |= 1u << PBTN_R;
        if (st.Gamepad.wButtons & XB_START) b |= 1u << PBTN_START;
        if (st.Gamepad.wButtons & XB_BACK)  b |= 1u << PBTN_BACK;
        if (st.Gamepad.bLeftTrigger > 128 && st.Gamepad.bRightTrigger > 128)
            b |= 1u << PBTN_FULL;
        *odir = d; *obtn = b;
        return;
    }

    if (gPadType == PAD_JOY) {
        JOYINFOEX ji;
        int cx, cy, dz;
        memset(&ji, 0, sizeof(ji));
        ji.dwSize  = sizeof(ji);
        ji.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx((UINT)gPadIdx, &ji) != JOYERR_NOERROR) { gPadType = PAD_NONE; return; }
        cx = (int)((gJoyCaps.wXmin + gJoyCaps.wXmax) / 2);
        cy = (int)((gJoyCaps.wYmin + gJoyCaps.wYmax) / 2);
        dz = (int)((gJoyCaps.wXmax - gJoyCaps.wXmin) / 4);
        if (dz < 1) dz = 1;
        if ((int)ji.dwXpos < cx - dz) d |= PDIR_L;
        if ((int)ji.dwXpos > cx + dz) d |= PDIR_R;
        if ((int)ji.dwYpos < cy - dz) d |= PDIR_U;
        if ((int)ji.dwYpos > cy + dz) d |= PDIR_D;
        if ((gJoyCaps.wCaps & JOYCAPS_HASPOV) && ji.dwPOV != JOY_POVCENTERED &&
            ji.dwPOV != 0xFFFF) {
            int deg = (int)ji.dwPOV / 100;
            if (deg > 225 && deg < 315) d |= PDIR_L;
            if (deg >  45 && deg < 135) d |= PDIR_R;
            if (deg > 315 || deg <  45) d |= PDIR_U;
            if (deg > 135 && deg < 225) d |= PDIR_D;
        }
        /*  a plain joystick has no names on its buttons, so they are
            handed over in the order the driver reports them and the
            config screen sorts out what each one does.             */
        if (ji.dwButtons & 0x01) b |= 1u << PBTN_A;
        if (ji.dwButtons & 0x02) b |= 1u << PBTN_B;
        if (ji.dwButtons & 0x04) b |= 1u << PBTN_X;
        if (ji.dwButtons & 0x08) b |= 1u << PBTN_Y;
        if (ji.dwButtons & 0x10) b |= 1u << PBTN_L;
        if (ji.dwButtons & 0x20) b |= 1u << PBTN_R;
        if (ji.dwButtons & 0x40) b |= 1u << PBTN_BACK;
        if (ji.dwButtons & 0x80) b |= 1u << PBTN_START;
        *odir = d; *obtn = b;
        return;
    }
}

/* one poll per logic tick; the bindings turn this into actions */
static void padUpdate(void)
{
    unsigned dir = 0, btn = 0;

    /* keep looking even when something is already selected */
    if (--gPadScan <= 0) {
        gPadScan = (gPadType == PAD_NONE) ? 20 : 60;
        padDetect();
    }
    if (gPadNotice > 0) gPadNotice--;
    padRead(&dir, &btn);

    gPadDirEdge = dir & ~gPadPrevDir;
    gPadBtnEdge = btn & ~gPadPrevBtn;
    gPadDir     = dir;
    gPadBtn     = btn;
    gPadPrevDir = dir;
    gPadPrevBtn = btn;

    if (gPadBtnEdge & (1u << PBTN_FULL)) toggleFullscreen();
    /*  back leaves the screen you are on, unless the player has given
        that button a job of its own.                                */
    if ((gPadBtnEdge & (1u << PBTN_BACK)) && !padBtnBound(PBTN_BACK)) doBack();
}

/* ------------------------------------------------------------------ */
/*  drawing helpers                                                    */
/* ------------------------------------------------------------------ */
static unsigned palColor(char ch, unsigned cm, unsigned cd)
{
    switch (ch) {
    case 'k': return 0x181420;
    case 'w': return 0xFFFFFF;
    case 'e': return 0x201830;
    case 'c': return cm;
    case 'C': return cd;
    case 'r': return 0xE03A2E;
    case 'R': return 0x8C1A14;
    case 'g': return 0x36B24A;
    case 'n': return 0xB2743A;
    case 'N': return 0x6E4320;
    case 'y': return 0xF2D06B;
    case 'o': return 0xF08A2B;
    case 'b': return 0x3C6BE0;
    case 'E': return 0x8A8FA0;
    case 'W': return 0xD6DAE6;
    case 'D': return 0x4A4F5E;
    }
    return 0xFF00FF;
}

static void drawSpriteS(int px, int py, const char **spr, unsigned cm, unsigned cd,
                        int flip, int xs, int ys)
{
    int r, c, sx, sy, dx, dy;
    for (r = 0; r < 16; r++) {
        const char *row = spr[r];
        int len = (int)strlen(row);
        for (c = 0; c < 16 && c < len; c++) {
            char ch = row[flip ? (15 - c < len ? 15 - c : c) : c];
            unsigned col;
            if (ch == '.') continue;
            col = palColor(ch, cm, cd);
            dx = px + c * xs;
            dy = py + r * ys;
            for (sy = 0; sy < ys; sy++) {
                int yy = dy + sy;
                if (yy < 0 || yy >= SCR_H) continue;
                for (sx = 0; sx < xs; sx++) {
                    int xx = dx + sx;
                    if (xx < 0 || xx >= SCR_W) continue;
                    gPix[yy * SCR_W + xx] = col;
                }
            }
        }
    }
}

static void drawSprite(int px, int py, const char **spr, unsigned cm, unsigned cd, int flip)
{
    drawSpriteS(px, py, spr, cm, cd, flip, 2, 2);
}

/*  the sheet art keeps its own colours, so it is looked up by
    character rather than tinted at draw time. one table, filled once,
    turns a character straight into a pixel.                        */
static unsigned artLut[128];
static unsigned logoLut[128];

static void artInit(void)
{
    int i;
    for (i = 0; i < 128; i++) artLut[i] = 0xFF00FF;
    for (i = 0; i < ART_NPAL; i++) artLut[(unsigned char)ART_KEY[i]] = ART_PAL[i];
    for (i = 0; i < 128; i++) logoLut[i] = 0xFF00FF;
    for (i = 0; i < LOGO_NPAL; i++) logoLut[(unsigned char)ART_KEY[i]] = LOGO_PAL[i];
}

/*  the logo blown up by a whole number, centred on cx. a fractional
    scale would put ragged edges on artwork whose whole character is
    its square dots, so the caller picks 3 or 4 and never between. */
static void drawLogo(int cx, int top, int sc)
{
    int r, c, sx, sy, x0 = cx - LOGO_W * sc / 2;
    for (r = 0; r < LOGO_H; r++) {
        const char *row = LOGO_PX[r];
        for (c = 0; c < LOGO_W; c++) {
            unsigned col;
            if (row[c] == '.') continue;
            col = logoLut[(unsigned char)row[c]];
            for (sy = 0; sy < sc; sy++) {
                int yy = top + r * sc + sy;
                if (yy < 0 || yy >= SCR_H) continue;
                for (sx = 0; sx < sc; sx++) {
                    int xx = x0 + c * sc + sx;
                    if (xx < 0 || xx >= SCR_W) continue;
                    gPix[yy * SCR_W + xx] = col;
                }
            }
        }
    }
}

/*  bx, by is the actor box on screen and the frame places itself from
    there. a flip mirrors it about the middle of that box, so a pose
    drawn facing right faces left without a second copy of the art. */
static void drawArt(int bx, int by, int id, int flip)
{
    const Art *a = &ART[id];
    int r, c, x0 = flip ? bx + AW - a->ox - a->w : bx + a->ox;
    for (r = 0; r < a->h; r++) {
        const char *row = a->px[r];
        int yy = by + a->oy + r;
        if (yy < 0 || yy >= SCR_H) continue;
        for (c = 0; c < a->w; c++) {
            char ch = row[flip ? a->w - 1 - c : c];
            int xx = x0 + c;
            if (ch == '.') continue;
            if (xx < 0 || xx >= SCR_W) continue;
            gPix[yy * SCR_W + xx] = artLut[(unsigned char)ch];
        }
    }
}

static void fillRect(int x, int y, int w, int h, unsigned col)
{
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            gPix[(y + j) * SCR_W + (x + i)] = col;
}

/*  world x to screen x. the shortest way round is the one on screen,
    because half the world (1024px) is wider than the window (960px),
    so anything visible can only be reached one way.                 */
static int scrX(float wx)
{
    float d = wx - gCamX;
    if (d >=  (float)(WORLD_W / 2)) d -= (float)WORLD_W;
    if (d <  -(float)(WORLD_W / 2)) d += (float)WORLD_W;
    return (int)floorf(d) + gShakeX;
}

/*  text goes through the wide API so the game can speak japanese.
    source strings are utf-8 and converted on the way out.          */
static int toWide(const char *s, WCHAR *out, int cap)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
    return (n > 0) ? n - 1 : 0;
}

static int textWidth(const char *s, int big)
{
    WCHAR w[256];
    SIZE sz;
    HFONT old;
    int n = toWide(s, w, 256);
    old = (HFONT)SelectObject(gMemDC, big ? gFontBig : gFontSml);
    GetTextExtentPoint32W(gMemDC, w, n, &sz);
    SelectObject(gMemDC, old);
    return (int)sz.cx;
}

/*  pick whatever japanese face this machine actually has. the same font
    is exposed under an english and a localised name depending on the
    system locale, so both spellings are probed.                        */
static int CALLBACK faceProbe(const LOGFONTW *lf, const TEXTMETRICW *tm,
                              DWORD type, LPARAM lp)
{
    (void)lf; (void)tm; (void)type;
    *(int *)lp = 1;
    return 0;
}

static int faceExists(const WCHAR *name)
{
    LOGFONTW lf;
    int found = 0;
    HDC dc = GetDC(NULL);
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy(lf.lfFaceName, name, LF_FACESIZE - 1);
    EnumFontFamiliesExW(dc, &lf, faceProbe, (LPARAM)&found, 0);
    ReleaseDC(NULL, dc);
    return found;
}

static WCHAR gFace[LF_FACESIZE] = L"Yu Gothic UI";

static void pickFace(void)
{
    static const char *cand[] = {
        "\xef\xbc\xad\xef\xbc\xb3 \xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf",
        "MS Gothic",
        "MS UI Gothic",
        "Yu Gothic UI",
        "\xe6\xb8\xb8\xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf",
        "Meiryo",
        "\xe3\x83\xa1\xe3\x82\xa4\xe3\x83\xaa\xe3\x82\xaa"
    };
    WCHAR w[LF_FACESIZE];
    int i;
    for (i = 0; i < (int)(sizeof(cand) / sizeof(cand[0])); i++) {
        if (toWide(cand[i], w, LF_FACESIZE) && faceExists(w)) {
            wcscpy(gFace, w);
            return;
        }
    }
}

static void drawText(const char *s, int x, int y, unsigned col, int big, int center)
{
    WCHAR w[256];
    RECT rc;
    HFONT old;
    int n = toWide(s, w, 256);
    old = (HFONT)SelectObject(gMemDC, big ? gFontBig : gFontSml);
    SetBkMode(gMemDC, TRANSPARENT);
    SetTextColor(gMemDC, RGB((col >> 16) & 255, (col >> 8) & 255, col & 255));
    rc.left = center ? 0 : x;
    rc.right = SCR_W;
    rc.top = y; rc.bottom = y + 64;
    DrawTextW(gMemDC, w, n, &rc,
              DT_TOP | (center ? DT_CENTER : DT_LEFT) | DT_SINGLELINE | DT_NOCLIP);
    SelectObject(gMemDC, old);
}

/*  drawText centres over the whole screen or hugs the left edge. a
    popup has to sit over the tile it came from, so it gets its own
    little box to centre inside, clamped to stay on screen.         */
static void drawTextBox(const char *s, int x, int y, int w, unsigned col)
{
    WCHAR wc[64];
    RECT rc;
    HFONT old;
    int n = toWide(s, wc, 64);
    if (x < 2) x = 2;
    if (x + w > SCR_W - 2) x = SCR_W - 2 - w;
    old = (HFONT)SelectObject(gMemDC, gFontSml);
    SetBkMode(gMemDC, TRANSPARENT);
    SetTextColor(gMemDC, RGB((col >> 16) & 255, (col >> 8) & 255, col & 255));
    rc.left = x; rc.right = x + w;
    rc.top = y; rc.bottom = y + 24;
    DrawTextW(gMemDC, wc, n, &rc, DT_TOP | DT_CENTER | DT_SINGLELINE | DT_NOCLIP);
    SelectObject(gMemDC, old);
}

/* ------------------------------------------------------------------ */
/*  map helpers                                                        */
/* ------------------------------------------------------------------ */
/*  the world has no ends: column -1 is column MAPW-1. everything that
    asks a question about x has to go through one of these three.     */
static float wrapW(float x)
{
    while (x <  0.0f)            x += (float)WORLD_W;
    while (x >= (float)WORLD_W)  x -= (float)WORLD_W;
    return x;
}
static int wrapTX(int tx)
{
    tx %= MAPW;
    return (tx < 0) ? tx + MAPW : tx;
}
/*  the shortest signed step from a to b, seam included - this is what
    keeps "which way is the player" honest for a foe standing on one
    side of the join with the player on the other.                   */
static float wrapDX(float a, float b)
{
    float d = b - a;
    if (d >  (float)(WORLD_W / 2)) d -= (float)WORLD_W;
    if (d < -(float)(WORLD_W / 2)) d += (float)WORLD_W;
    return d;
}

static char tileAt(int tx, int ty)
{
    if (ty < 0)     return ' ';          /* open sky above the top deck */
    if (ty >= MAPH) return '#';
    return gmap[ty][wrapTX(tx)];
}
static int isSolid(char c) { return c == '#'; }
/*  a platform and the top of a ladder only hold you up from above  */
static int isOneWay(char c) { return c == '=' || c == 'H'; }

static void spawnActor(Actor *a, int tx, int ty)
{
    memset(a, 0, sizeof(*a));
    a->x = wrapW((float)(tx * TILE + (TILE - AW) / 2));
    a->y = (float)(ty * TILE + (TILE - AH));
    a->dir = 1;
    a->alive = 1;
    a->ride = -1;
}

static void popClear(void);

static void loadStage(int idx)
{
    int r, c, i;
    const char **src = STAGES[idx % NSTAGE];
    gFoeCount = 0;
    gFruit = 0;
    popClear();
    gGoalX = gGoalY = -1;
    gPlayerSX = 1; gPlayerSY = MAPH - 2;

    for (r = 0; r < MAPH; r++) {
        int len = (int)strlen(src[r]);
        for (c = 0; c < MAPW; c++) gmap[r][c] = (c < len) ? src[r][c] : ' ';
        gmap[r][MAPW] = 0;
    }
    for (r = 0; r < MAPH; r++) {
        for (c = 0; c < MAPW; c++) {
            char t = gmap[r][c];
            if (t == 'P') { gPlayerSX = c; gPlayerSY = r; gmap[r][c] = ' '; }
            else if (t == '1') {
                if (gFoeCount < MAXENEMY) {
                    spawnActor(&gFoe[gFoeCount], c, r);
                    gFoe[gFoeCount].dir = (gFoeCount & 1) ? -1 : 1;
                    gFoeCount++;
                }
                gmap[r][c] = ' ';
            }
            else if (t == 'o' || t == 'Q') gFruit++;
            else if (t == 'G') { gGoalX = c; gGoalY = r; }
        }
    }
    if (gGoalX < 0) { gGoalX = MAPW - 3; gGoalY = 1; gmap[gGoalY][gGoalX] = 'G'; }
    spawnActor(&gPlayer, gPlayerSX, gPlayerSY);
    gPlayer.invuln = INVULN;
    gLife = LIFE_MAX;                     /* hearts refill every stage */
    for (i = 0; i < gFoeCount; i++) {
        gFoe[i].think  = 30 + i * 17;
        gFoe[i].temper = (i + idx) % 3;   /* wanderer .. hunter */
        gFoe[i].mode   = FM_PATROL;
        gFoe[i].modeT  = 60 + i * 40;
    }
    gTime = 330;                          /* the world got a lot wider */
    gTimeTick = 0;
    gCamX = wrapW(gPlayer.x + AW * 0.5f - SCR_W * 0.5f);
    gShakeT = gFreeze = gHurtT = 0;
}

static void restartStage(void)
{
    /* keep collected fruit? no - classic behaviour: full reset of the stage */
    loadStage(gStage);
}

/* ------------------------------------------------------------------ */
/*  physics                                                            */
/* ------------------------------------------------------------------ */
static void moveX(Actor *a)
{
    float nx = a->x + a->vx;
    int i;
    if (a->vx == 0) return;
    for (i = 0; i < 3; i++) {
        int py = (int)a->y + (i == 0 ? 0 : (i == 1 ? AH / 2 : AH - 1));
        /*  nx dips just below zero when something walks off the left
            hand seam, and a cast would round that towards zero and
            read the wrong column - floor it.                       */
        int px = (a->vx > 0) ? (int)floorf(nx) + AW - 1 : (int)floorf(nx);
        if (isSolid(tileAt(px >> 5, py >> 5))) {
            int tx = px >> 5;
            nx = (a->vx > 0) ? (float)(tx * TILE - AW) : (float)((tx + 1) * TILE);
            a->vx = 0;
            break;
        }
    }
    a->x = wrapW(nx);
}

static void moveY(Actor *a)
{
    float ny = a->y + a->vy;
    int i;
    a->onground = 0;
    if (a->vy > 0) {
        int botOld = (int)a->y + AH - 1;
        int botNew = (int)ny + AH - 1;
        int ty = botNew >> 5;
        for (i = 0; i < 3; i++) {
            int px = (int)a->x + (i == 0 ? 1 : (i == 1 ? AW / 2 : AW - 2));
            char t = tileAt(px >> 5, ty);
            int hit = isSolid(t);
            /*  a platform stops a fall that started above it, and DOWN
                switches that off for a dozen frames so the player can
                step off the deck instead of only jumping up to it.  */
            if (!hit && isOneWay(t) && !a->climb && a->dropThru <= 0 &&
                botOld < (ty * TILE)) hit = 1;
            if (hit) {
                ny = (float)(ty * TILE - AH);
                a->vy = 0;
                a->onground = 1;
                break;
            }
        }
    } else if (a->vy < 0) {
        int ty = ((int)ny) >> 5;
        for (i = 0; i < 3; i++) {
            int px = (int)a->x + (i == 0 ? 1 : (i == 1 ? AW / 2 : AW - 2));
            if (isSolid(tileAt(px >> 5, ty))) {
                ny = (float)((ty + 1) * TILE);
                a->vy = 0;
                break;
            }
        }
    }
    a->y = ny;
    if (a->y < 0) { a->y = 0; if (a->vy < 0) a->vy = 0; }
    if (a->y > MAPH * TILE) a->y = (float)(MAPH * TILE - AH);
}

static int centerTile(Actor *a, int *tx, int *ty)
{
    *tx = wrapTX(((int)a->x + AW / 2) >> 5);
    *ty = ((int)a->y + AH / 2) / TILE;
    return 1;
}

static int onLadder(Actor *a)
{
    int tx, ty;
    centerTile(a, &tx, &ty);
    return tileAt(tx, ty) == 'H';
}
static int ladderBelow(Actor *a)
{
    int tx = ((int)a->x + AW / 2) >> 5;
    int ty = ((int)a->y + AH + 2) / TILE;
    return tileAt(tx, ty) == 'H';
}

/*  a ladder runs all the way down to the last walkable row, so an actor
    resting on the floor at the foot of one still has its centre inside
    an 'H' tile. "am i standing on something solid" therefore cannot be
    answered with onLadder/ladderBelow alone - probe the pixel row under
    the feet directly. groundProbe cannot serve here because it reports
    0 for anything that is climbing, which is exactly the case we need
    to break out of.                                                   */
static int floorBelow(Actor *a)
{
    int i;
    int probe   = (int)a->y + AH;              /* first pixel below feet */
    int feetRow = ((int)a->y + AH - 1) >> 5;
    for (i = 0; i < 3; i++) {
        int px = (int)a->x + (i == 0 ? 1 : (i == 1 ? AW / 2 : AW - 2));
        char t = tileAt(px >> 5, probe >> 5);
        if (isSolid(t)) return 1;
        if (isOneWay(t) && feetRow < (probe >> 5)) return 1;
    }
    return 0;
}

/*  standing on a plank rather than on the ground - the one case where
    pressing DOWN has somewhere to go.                               */
static int platformBelow(Actor *a)
{
    int i, found = 0;
    int probe   = (int)a->y + AH;
    int feetRow = ((int)a->y + AH - 1) >> 5;
    if (feetRow >= (probe >> 5)) return 0;     /* not resting on the seam */
    for (i = 0; i < 3; i++) {
        int px = (int)a->x + (i == 0 ? 1 : (i == 1 ? AW / 2 : AW - 2));
        char t = tileAt(px >> 5, probe >> 5);
        if (isSolid(t)) return 0;              /* solid ground wins */
        if (t == '=')   found = 1;
    }
    return found;
}

/*  the old code only called an actor "grounded" when a downward move
    ended inside a tile. resting on the floor gravity only builds up
    0.55px per frame, so the feet needed two frames to reach the next
    tile row and the flag flickered 1,0,1,0 - which silently ate every
    other jump press. probing the pixel row under the feet instead is
    stable and doubles as coyote time when stepping off a ledge.       */
static int groundProbe(Actor *a)
{
    int i;
    int probe   = (int)a->y + AH;              /* first pixel below feet */
    int feetRow = ((int)a->y + AH - 1) >> 5;
    if (a->climb)      return 0;
    if (a->vy < -0.01f) return 0;              /* still rising */
    for (i = 0; i < 3; i++) {
        int px = (int)a->x + (i == 0 ? 1 : (i == 1 ? AW / 2 : AW - 2));
        char t = tileAt(px >> 5, probe >> 5);
        if (isSolid(t)) return 1;
        if (a->dropThru > 0) continue;         /* falling past the planks */
        if (isOneWay(t) && feetRow < (probe >> 5)) return 1;
    }
    return 0;
}

static int actorsHit(Actor *a, Actor *b)
{
    return fabsf(wrapDX(a->x, b->x)) <= (float)(AW - 2 * HITX) &&
           fabsf(b->y - a->y)        <= (float)(AH - 2 * HITY);
}

/* ------------------------------------------------------------------ */
/*  game logic                                                         */
/* ------------------------------------------------------------------ */
static void hurtFx(float x, float y);

static void killPlayer(void)
{
    if (gState != ST_PLAY) return;
    gPlayer.alive = 0;
    gState = ST_DEAD;
    gStateT = 0;
    gShakeT = HURT_SHAKE + 6;
    playSong(SONG_NONE);
    playSfx(SFX_DIE);
}

/*  a touch costs one heart instead of the whole life. the player is
    bumped away and blinks, so a crowd cannot drain the bar at once.  */
static void hurtPlayer(float fromX)
{
    Actor *p = &gPlayer;
    if (gState != ST_PLAY || p->invuln > 0) return;
    gLife--;
    p->invuln   = HURT_INV;
    p->climb    = 0;
    p->ride     = -1;
    p->dropThru = 0;
    p->dash     = 0;
    p->vy       = -6.5f;
    p->vx       = 0;
    /*  bumped away from whatever landed the hit, the short way round  */
    p->x = wrapW(p->x + ((wrapDX(p->x, fromX) > 0.0f) ? -14.0f : 14.0f));
    hurtFx(p->x + AW / 2, p->y + AH / 2);
    if (gLife <= 0) killPlayer();
    else            playSfx(SFX_HURT);
}

/* the box the extended foot sweeps through */
static void kickBox(Actor *p, int *bx, int *by, int *bw, int *bh)
{
    *bw = KICK_W;
    *bh = KICK_H;
    *by = (int)p->y + 5;
    *bx = (p->dir > 0) ? (int)p->x + AW - 5 : (int)p->x - KICK_W + 5;
}

/* ------------------------------------------------------------------ */
/*  dash effects                                                       */
/* ------------------------------------------------------------------ */
/*  pure decoration - nothing here feeds back into the simulation, so a
    dropped particle can never change where the player ends up. the
    trail is drawn as plain blobs rather than copies of the sprite:
    the outline and the eyes come from fixed palette entries, so a
    dimmed sprite would still have hard black edges and read as a
    second player instead of an afterimage.                          */
typedef struct { float x, y; int life; } Ghost;
typedef struct { float x, y, vx, vy; int life, size; unsigned col; } Puff;

static Ghost gGhost[MAXGHOST];
static Puff  gPuff[MAXPUFF];

static void dashFxClear(void)
{
    memset(gGhost, 0, sizeof(gGhost));
    memset(gPuff,  0, sizeof(gPuff));
}

static void ghostSpawn(float x, float y)
{
    int i, slot = 0, worst = 0x7fffffff;
    for (i = 0; i < MAXGHOST; i++) {
        if (gGhost[i].life <= 0) { slot = i; break; }
        if (gGhost[i].life < worst) { worst = gGhost[i].life; slot = i; }
    }
    gGhost[slot].x = x; gGhost[slot].y = y;
    gGhost[slot].life = GHOST_LIFE;
}

/*  a burst of dust thrown out behind the take off, spread over a
    quarter turn so it looks kicked rather than sprayed.            */
static void puffBurst(float x, float y, int dir)
{
    int i;
    for (i = 0; i < 9; i++) {
        int slot = -1, j, worst = 0x7fffffff;
        float sp = 1.3f + (rand() % 100) * 0.016f;
        float sy = ((rand() % 100) - 50) * 0.022f;
        for (j = 0; j < MAXPUFF; j++) {
            if (gPuff[j].life <= 0) { slot = j; break; }
            if (gPuff[j].life < worst) { worst = gPuff[j].life; slot = j; }
        }
        gPuff[slot].x    = x + (rand() % 9) - 4;
        gPuff[slot].y    = y + (rand() % 17) - 8;
        gPuff[slot].vx   = -dir * sp;
        gPuff[slot].vy   = sy - 0.25f;
        gPuff[slot].life = PUFF_LIFE - (rand() % 6);
        gPuff[slot].size = 2 + (rand() % 3);
        gPuff[slot].col  = 0xE8F0FF;
    }
}

/*  the damage burst: sparks thrown out in every direction, heavier and
    hotter than dust so the two never read as the same thing.        */
static void sparkBurst(float x, float y)
{
    int i;
    for (i = 0; i < 16; i++) {
        int slot = -1, j, worst = 0x7fffffff;
        double a  = (rand() % 628) * 0.01;
        float  sp = 1.7f + (rand() % 100) * 0.034f;
        for (j = 0; j < MAXPUFF; j++) {
            if (gPuff[j].life <= 0) { slot = j; break; }
            if (gPuff[j].life < worst) { worst = gPuff[j].life; slot = j; }
        }
        gPuff[slot].x    = x + (rand() % 9) - 4;
        gPuff[slot].y    = y + (rand() % 9) - 4;
        gPuff[slot].vx   = (float)cos(a) * sp;
        gPuff[slot].vy   = (float)sin(a) * sp - 0.7f;
        gPuff[slot].life = PUFF_LIFE + 8 - (rand() % 8);
        gPuff[slot].size = 2 + (rand() % 3);
        gPuff[slot].col  = (i & 1) ? 0xFF5A70 : 0xFFD060;
    }
}

static void hurtFx(float x, float y)
{
    sparkBurst(x, y);
    gHurtT  = HURT_FX;
    gShakeT = HURT_SHAKE;
    gFreeze = HURT_FREEZE;
}

static void dashFxUpdate(void)
{
    int i;
    for (i = 0; i < MAXGHOST; i++)
        if (gGhost[i].life > 0) gGhost[i].life--;
    for (i = 0; i < MAXPUFF; i++) {
        Puff *q = &gPuff[i];
        if (q->life <= 0) continue;
        q->life--;
        q->x  += q->vx;
        q->y  += q->vy;
        q->vx *= 0.88f;             /* drag, so it settles quickly */
        q->vy  = q->vy * 0.88f + 0.04f;
    }
}

/*  the frame buffer is plain 32 bit rgb with no alpha channel, so the
    effects blend themselves: read the pixel, mix, write it back. an
    opaque trail would punch dark holes in the ladders and fruit it
    passes over, which is exactly what an afterimage must not do.    */
static void blendRect(int x, int y, int w, int h, unsigned col, float a)
{
    int i, j;
    int cr = (col >> 16) & 255, cg = (col >> 8) & 255, cb = col & 255;
    if (a <= 0.004f) return;
    if (a > 1.0f) a = 1.0f;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    for (j = 0; j < h; j++) {
        unsigned *row = gPix + (y + j) * SCR_W + x;
        for (i = 0; i < w; i++) {
            unsigned d = row[i];
            int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
            row[i] = ((unsigned)(dr + (cr - dr) * a) << 16) |
                     ((unsigned)(dg + (cg - dg) * a) <<  8) |
                      (unsigned)(db + (cb - db) * a);
        }
    }
}

/* the player is a ball, so a rounded slab stands in for its silhouette */
static void drawBlob(int x, int y, unsigned col, float a)
{
    blendRect(x + 5,  y,      12, 2,  col, a);
    blendRect(x + 2,  y + 2,  18, 3,  col, a);
    blendRect(x,      y + 5,  22, 14, col, a);
    blendRect(x + 2,  y + 19, 18, 3,  col, a);
    blendRect(x + 5,  y + 22, 12, 2,  col, a);
}

static void drawDashFx(void)
{
    int i;
    for (i = 0; i < MAXGHOST; i++) {
        Ghost *g = &gGhost[i];
        float f;
        if (g->life <= 0) continue;
        f = g->life / (float)GHOST_LIFE;
        /*  half opacity at most, so the trail stays behind the player
            instead of competing with it for attention.              */
        drawBlob(scrX(g->x), gDrawY0 + (int)g->y + 2, 0x7CB0FF, f * f * 0.5f);
    }
    for (i = 0; i < MAXPUFF; i++) {
        Puff *q = &gPuff[i];
        float f;
        if (q->life <= 0) continue;
        f = q->life / (float)PUFF_LIFE;
        blendRect(scrX(q->x), gDrawY0 + (int)q->y, q->size, q->size,
                  q->col, f * 0.85f);
    }
}

/*  combo gauge over the player's head: how long is left to reach the
    next fruit, and what that fruit would then be worth. it lives on
    the player rather than in the hud so the decision to go for it is
    made without looking away from the action.                       */
static void drawComboGauge(void)
{
    Actor *p = &gPlayer;
    int   x, y, ty, w = 34, fw, i;
    float f, mult = 1.0f;
    unsigned col;
    char buf[24];

    if (gComboT <= 0 || !p->alive) return;
    f = gComboT / (float)COMBO_WIN;
    for (i = 0; i <= gCombo; i++) mult *= 1.5f;   /* what the next one pays */

    ty = gDrawY0 + (int)p->y - 36;
    if (ty < HUD_H + 1) ty = HUD_H + 1;
    y = ty + 21;
    x = scrX(p->x) + AW / 2 - w / 2;
    if (x < 2) x = 2;
    if (x + w > SCR_W - 2) x = SCR_W - 2 - w;

    if (f > 0.5f)       col = 0xFFD060;
    else if (f > 0.25f) col = 0xFF9A40;
    else                col = ((gFrame / 4) % 2) ? 0xFF5050 : 0xFFE0E0;

    fw = (int)(f * (w - 2) + 0.5f);
    if (fw < 1) fw = 1;
    fillRect(x - 1, y - 1, w + 2, 7, 0x101018);      /* frame */
    fillRect(x,     y,     w,     5, 0x2C2438);      /* empty track */
    fillRect(x + 1, y + 1, fw,    3, col);
    sprintf(buf, "x%.1f", mult);
    drawTextBox(buf, x + w / 2 - 30, ty, 60, col);
}

/* ------------------------------------------------------------------ */
/*  fruit, combo and score popups                                      */
/* ------------------------------------------------------------------ */
static void popClear(void)
{
    memset(gPop, 0, sizeof(gPop));
    gCombo  = 0;
    gComboT = 0;
    dashFxClear();
}

/*  the oldest slot is recycled when they all run at once, so a greedy
    sweep through a row of fruit never silently drops the newest one.  */
static void popSpawn(float x, float y, int score, float mult)
{
    int i, slot = 0, worst = 0x7fffffff;
    for (i = 0; i < MAXPOP; i++) {
        if (gPop[i].life <= 0) { slot = i; break; }
        if (gPop[i].life < worst) { worst = gPop[i].life; slot = i; }
    }
    gPop[slot].x     = x;
    gPop[slot].y     = y;
    gPop[slot].life  = POP_LIFE;
    gPop[slot].score = score;
    gPop[slot].mult  = mult;
}

static void popUpdate(void)
{
    int i;
    for (i = 0; i < MAXPOP; i++) {
        if (gPop[i].life <= 0) continue;
        gPop[i].life--;
        gPop[i].y -= 0.7f;           /* drifts up out of the way */
    }
    if (gComboT > 0 && --gComboT == 0) gCombo = 0;
    dashFxUpdate();
}

/*  one fruit taken, wherever it came from - walked into or kicked loose.
    a pickup inside the two second window extends the chain and multiplies
    the payout by 1.5 again, so the fourth fruit of a run is worth 3.375
    times its face value.                                               */
static void takeFruit(int c, int r, char t)
{
    int   base = (t == 'Q') ? 300 : 200;
    c = wrapTX(c);
    float mult = 1.0f;
    int   i, gain;

    if (gComboT > 0) gCombo++; else gCombo = 0;
    gComboT = COMBO_WIN;
    for (i = 0; i < gCombo; i++) mult *= 1.5f;

    gain = (int)(base * mult + 0.5f);
    gmap[r][c] = ' ';
    gFruit--;
    gScore += gain;
    popSpawn((float)(c * TILE + TILE / 2), (float)(r * TILE), gain,
             (gCombo > 0) ? mult : 1.0f);
    playSfx(SFX_COIN);
    if (gFruit == 0) playSfx(SFX_OPEN);
}

static void doKick(void)
{
    Actor *p = &gPlayer;
    int bx, by, bw, bh, i, r, c, r0, r1, c0, c1;
    kickBox(p, &bx, &by, &bw, &bh);

    for (i = 0; i < gFoeCount; i++) {
        Actor *e = &gFoe[i];
        /*  a dizzy one can be kicked again - it slides another two tiles
            and the two second count starts over. the mask stops a single
            kick from connecting once per active frame.                  */
        float dx = wrapDX((float)bx, e->x);   /* the foe, relative to the box */
        if (p->kickHit & (1 << i)) continue;
        if (dx > (float)(bw - 3) || dx + AW - 3 < 0.0f) continue;
        if (by + bh < e->y + 3 || e->y + AH - 3 < by) continue;
        p->kickHit |= (1 << i);
        e->stun     = STUN_LEN;
        e->climb    = 0;
        e->vx       = 0;
        e->vy       = 0;
        e->mode     = FM_PATROL;
        e->modeT    = STUN_LEN + 30;
        e->dir      = (p->dir > 0) ? 1 : -1;
        /*  slide the two tiles over KB_LEN frames instead of teleporting
            there. a second kick while it is still dizzy just restarts
            the slide, so it keeps sliding further.                  */
        e->kbT      = KB_LEN;
        e->kbDir    = e->dir;
        if (p->ride == i) p->ride = -1;
        playSfx(SFX_HIT);        /* stunning is its own reward - no points */
    }

    /* the foot knocks fruit loose as well */
    c0 = bx >> 5;    c1 = (bx + bw - 1) >> 5;
    r0 = by / TILE;  r1 = (by + bh - 1) / TILE;
    for (r = r0; r <= r1; r++) for (c = c0; c <= c1; c++) {
        char t = tileAt(c, r);
        if (t == 'o' || t == 'Q') takeFruit(c, r, t);
    }
}

/* head height of a foe - a stunned one is squashed to half */
static float foeTop(Actor *e) { return e->y + (e->stun > 0 ? (float)(AH / 2) : 0.0f); }

/*  land on a foe and ride it. checked after the player has moved, so
    it behaves like a one way platform that walks around.            */
static int rideEnemy(Actor *p)
{
    int i;
    if (p->vy < 0 || p->climb) return -1;
    for (i = 0; i < gFoeCount; i++) {
        Actor *e = &gFoe[i];
        float top  = foeTop(e);
        float feet = p->y + AH;
        if (fabsf(wrapDX(p->x, e->x)) > (float)(AW - 7)) continue;
        if (feet < top - 1.0f || feet > top + 14.0f) continue;
        p->y  = top - AH;
        p->vy = 0;
        return i;
    }
    return -1;
}

static void updatePlayer(void)
{
    Actor *p = &gPlayer;
    int left  = actDown(ACT_LEFT);
    int right = actDown(ACT_RIGHT);
    int up    = actDown(ACT_UP);
    int down  = actDown(ACT_DOWN);
    int jumpPress, jumpHeld, wasGround, dashPress;
    int tx, ty;

    /*  up is climb only. it used to double as jump, which made walking
        under a ladder launch you by accident.                          */
    jumpPress = actHit(ACT_JUMP);
    jumpHeld  = actDown(ACT_JUMP);
    dashPress = actHit(ACT_DASH);

    if (p->dropThru > 0) p->dropThru--;

    /* kick - available on the ground, in the air and on a ladder */
    if (p->kick > 0) {
        p->kick--;
        if (p->kick > KICK_DUR - KICK_ACT) doKick();
        if (p->kick == 0) p->kickCd = KICK_CD;
    } else if (p->kickCd > 0) {
        p->kickCd--;
    } else if (actHit(ACT_KICK)) {
        p->kick    = KICK_DUR;
        p->kickHit = 0;
        playSfx(SFX_KICK);
    }

    /*  dash - a direction plus the dash button, on the ground, in the
        air or off a ladder. it is not a committed three tiles: tapping
        the other way cuts it short there and then, which is what makes
        it usable for dodging rather than only for covering ground.
        the cool down starts when the dash ends, however it ended.     */
    if (p->dashCd > 0) p->dashCd--;
    if (p->dash > 0) {
        if ((p->dashDir > 0 && left && !right) || (p->dashDir < 0 && right && !left)) {
            p->dash   = 0;                  /* cancelled by the player */
            p->dashCd = DASH_CD;
        } else {
            p->dash--;
            if ((p->dash & 1) == 0) ghostSpawn(p->x, p->y);
            if (p->dash == 0) p->dashCd = DASH_CD;
        }
    } else if (dashPress && p->dashCd == 0 && (left || right)) {
        p->dashDir = (left && !right) ? -1 : (right && !left) ? 1 : p->dir;
        p->dash    = DASH_LEN;
        p->dir     = p->dashDir;
        p->climb   = 0;                     /* lets go of a ladder */
        p->vy      = 0;
        ghostSpawn(p->x, p->y);
        puffBurst(p->x + AW / 2 - p->dashDir * 4.0f, p->y + AH / 2, p->dashDir);
        playSfx(SFX_DASH);
    }

    /* carried along by whatever is being stood on */
    if (p->ride >= 0 && p->ride < gFoeCount) {
        Actor *e = &gFoe[p->ride];
        float feet = p->y + AH, top = foeTop(e);
        if (feet < top - 3.0f || feet > top + 6.0f ||
            fabsf(wrapDX(p->x, e->x)) > (float)(AW - 7)) {
            p->ride = -1;
        } else {
            p->x = wrapW(p->x + e->vx);
        }
    }

    wasGround   = p->onground;
    p->onground = groundProbe(p) || p->ride >= 0;
    if (p->onground) p->coyote = COYOTE; else if (p->coyote > 0) p->coyote--;
    if (jumpPress)   p->jbuf   = JBUF;   else if (p->jbuf   > 0) p->jbuf--;
    if (p->invuln > 0) p->invuln--;
    if (!wasGround && p->onground && p->vy >= 0) playSfx(SFX_LAND);

    p->vx = 0;
    if (left)  { p->vx = -PSPEED; p->dir = -1; }
    if (right) { p->vx =  PSPEED; p->dir =  1; }

    /* ladder handling */
    if (p->climb) {
        p->vy = 0;
        if (up)   p->vy = -PCLIMB;
        if (down) p->vy =  PCLIMB;
        if (!onLadder(p) && !(down && ladderBelow(p))) p->climb = 0;
        if (p->jbuf > 0) {                       /* hop off the ladder */
            p->climb = 0; p->jbuf = 0; p->coyote = 0;
            p->vy = PJUMP * 0.82f;
            playSfx(SFX_JUMP);
        }
    } else {
        /* a dash in progress must not re-grab the ladder it just left */
        if (up && onLadder(p) && !p->dash)          { p->climb = 1; p->vy = 0; p->jbuf = 0; }
        else if (down && ladderBelow(p) && !p->dash){ p->climb = 1; p->vy = 0; p->jbuf = 0; p->y += 2; }
        else {
            /*  a ladder always wins, so this only fires on a bare plank:
                DOWN turns the platforms off for a moment and the player
                simply falls off the deck. the jump below is skipped for
                that frame, which is what makes DOWN+JUMP a drop rather
                than a hop.                                            */
            if (down && p->onground && p->ride < 0 && !p->dash && platformBelow(p)) {
                p->dropThru = DROPTHRU;
                p->onground = 0;
                p->coyote   = 0;
                p->jbuf     = 0;
                p->y       += 2.0f;
            }
            if (p->jbuf > 0 && p->coyote > 0) {
                p->vy = PJUMP;
                p->jbuf = 0; p->coyote = 0; p->onground = 0;
                playSfx(SFX_JUMP);
            }
            /* let go early for a short hop */
            if (!jumpHeld && p->vy < JUMPCUT) p->vy = JUMPCUT;
            p->vy += (p->vy < 0.0f) ? GRAV_UP : GRAV_DN;
            if (p->vy > MAXFALL) p->vy = MAXFALL;
        }
    }

    /*  a dash flies flat - it overrides the walking speed and holds the
        fall still, in the air just as much as on the ground. jumping
        out of one is allowed and ends it, so a jump pressed mid dash is
        never quietly swallowed.                                      */
    if (p->dash > 0) {
        if (p->vy < 0.0f) { p->dash = 0; p->dashCd = DASH_CD; }
        else { p->vx = p->dashDir * DASH_SPD; p->vy = 0; }
    }

    moveX(p);
    moveY(p);
    if (p->ride >= 0 && p->onground && p->vy >= 0) p->vy = 0;
    p->ride = rideEnemy(p);
    p->onground = groundProbe(p) || p->ride >= 0;

    /* fruit pickup */
    {
        int c0 = ((int)p->x) >> 5, c1 = ((int)p->x + AW - 1) >> 5;
        int r0 = (int)p->y / TILE, r1 = ((int)p->y + AH - 1) / TILE;
        int r, c;
        for (r = r0; r <= r1; r++) for (c = c0; c <= c1; c++) {
            char t = tileAt(c, r);
            if (t == 'o' || t == 'Q') takeFruit(c, r, t);
        }
    }

    /* spikes */
    centerTile(p, &tx, &ty);
    if (tileAt(tx, ty) == '^' || tileAt(tx, (((int)p->y + AH - 2) / TILE)) == '^') {
        hurtPlayer(p->x + p->dir * 24.0f);   /* bounced back off the spikes */
        if (gState != ST_PLAY) return;
    }

    /* goal */
    if (gFruit <= 0 && gGoalX >= 0) {
        float dx = wrapDX(p->x, (float)(gGoalX * TILE));
        float gy = (float)(gGoalY * TILE);
        if (dx < (float)(AW - 6) && dx > (float)(6 - TILE) &&
            p->y + AH - 6 > gy && p->y + 6 < gy + TILE) {
            gState = ST_CLEAR;
            gStateT = 0;
            playSong(SONG_CLEAR);
        }
    }
    p->anim++;
}

static void updateFoe(Actor *e, int idx)
{
    Actor *p = &gPlayer;
    float spd = 1.15f + gLoop * 0.18f + (gStage > 3 ? 0.10f : 0.0f);
    int tx, ty, ptx, pty;
    centerTile(e, &tx, &ty);
    centerTile(p, &ptx, &pty);
    e->onground = groundProbe(e);

    /* seeing birds: no thinking, just fall and sit there */
    if (e->stun > 0) {
        e->stun--;
        /*  ride out the kick: an easing slide across the two tiles, one
            frame at a time, so walls stop it and the trip is actually
            drawn instead of the foe blinking to the far side.       */
        if (e->kbT > 0) {
            e->vx = e->kbDir * (KB_STEP * e->kbT);
            e->kbT--;
            moveX(e);
            if (e->vx == 0.0f) e->kbT = 0;      /* ran into a wall */
        } else {
            e->vx = 0;
        }
        e->climb = 0;
        e->vy += GRAV_DN;
        if (e->vy > MAXFALL) e->vy = MAXFALL;
        moveY(e);
        e->onground = groundProbe(e);
        if (e->onground) e->vy = 0;
        e->anim++;
        return;
    }

    /*  temperament: every so often a foe re-decides whether to hunt the
        player or just wander. a wanderer keeps its heading until it is
        stopped by a wall or a drop, which reads far less robotic than
        everyone beelining at you.                                      */
    if (--e->modeT <= 0) {
        int dist   = ((int)fabsf(wrapDX(e->x, p->x)) + abs((int)p->y - (int)e->y)) / TILE;
        int hunt   = 20 + e->temper * 22 + (dist < 7 ? 25 : 0) + gLoop * 10;
        e->mode    = ((rand() % 100) < hunt) ? FM_CHASE : FM_PATROL;
        e->modeT   = (e->mode == FM_CHASE) ? (90 + rand() % 90) : (140 + rand() % 160);
        if (e->mode == FM_PATROL && (rand() & 3) == 0) e->dir = -e->dir;
    }

    if (e->climb) {
        float colc = (float)(tx * TILE + (TILE - AW) / 2);
        e->x += (colc - e->x) * 0.4f;
        e->vx = 0;
        e->vy = (e->climbDir > 0) ? spd : -spd;
        if (e->climbDir < 0) {
            if (!onLadder(e)) { e->climb = 0; e->vy = 0; }
        } else {
            /*  down: let go at the foot of the ladder. the old test only
                looked for "no ladder here and none below", but a ladder
                bottoms out on the last walkable row, so standing on the
                floor there still counts as onLadder and the foe stayed
                latched in climb state for good - the only way out was
                the 1-in-32 roll below, which is gated on the player
                sharing its row. checking for solid ground under the
                feet is what actually ends the descent.               */
            if (!onLadder(e) && !ladderBelow(e)) { e->climb = 0; e->vy = 0; }
            else if (!ladderBelow(e) && floorBelow(e)) { e->climb = 0; e->vy = 0; }
        }
        if (pty == ty && e->climb) {
            if (onLadder(e) && (rand() & 31) == 0) { e->climb = 0; e->vy = 0; }
        }
        moveY(e);
        e->onground = groundProbe(e);
        e->anim++;
        return;
    }

    /* walking: only a hunter steers towards the player */
    if (--e->think <= 0) {
        e->think = 10 + (rand() % 16);
        if (e->mode == FM_CHASE) {
            float d = wrapDX(e->x, p->x);
            if (d < -6.0f)     e->dir = -1;
            else if (d > 6.0f) e->dir =  1;
        }
    }

    /* ladder decision when near a column centre */
    {
        float colc = (float)(tx * TILE + (TILE - AW) / 2);
        if (fabsf(e->x - colc) < 5.0f && e->onground) {
            if (e->mode == FM_CHASE) {
                if (pty < ty && onLadder(e)) { e->climb = 1; e->climbDir = -1; }
                else if (pty > ty && ladderBelow(e)) { e->climb = 1; e->climbDir = 1; e->y += 2; }
            } else if ((rand() % 100) < 4) {          /* wander up or down */
                if ((rand() & 1) && onLadder(e))      { e->climb = 1; e->climbDir = -1; }
                else if (ladderBelow(e))              { e->climb = 1; e->climbDir = 1; e->y += 2; }
            }
        }
    }
    if (e->climb) return;

    e->vx = e->dir * spd;
    e->vy += GRAV_DN;
    if (e->vy > MAXFALL) e->vy = MAXFALL;

    /* wall / ledge check */
    {
        int aheadX = (e->dir > 0) ? ((int)e->x + AW + 2) : ((int)e->x - 2);
        int footY  = (int)e->y + AH + 4;
        char ahead = tileAt(aheadX >> 5, ((int)e->y + AH / 2) / TILE);
        char below = tileAt(aheadX >> 5, footY / TILE);
        if (e->drop > 0) e->drop--;
        /*  a wall always turns it round. at a ledge the choice is made
            once and then stuck to - re-rolling every frame meant it
            always chickened out before actually reaching the edge.     */
        if (isSolid(ahead) || ahead == '^') { e->dir = -e->dir; e->think = 12; e->drop = 0; }
        else if (e->onground && !isSolid(below) && !isOneWay(below) && e->drop == 0) {
            if (e->mode == FM_PATROL || (rand() % 100) < 55) { e->dir = -e->dir; e->think = 12; }
            else e->drop = 30;              /* hunter commits to the fall */
        }
        /*  no edge of the world to turn round at any more - a foe that
            keeps walking simply comes back round the other side.     */
    }

    moveX(e);
    moveY(e);
    e->onground = groundProbe(e);
    e->anim++;
    (void)idx;
}

static void nextStage(void)
{
    gStage++;
    if (gStage >= NSTAGE) {
        gStage = 0;
        gLoop++;
        gState = ST_ALLCLEAR;
        gStateT = 0;
        playSong(SONG_CLEAR);
        return;
    }
    loadStage(gStage);
    gState = ST_READY;
    gStateT = 0;
    playSong(SONG_NONE);
}

static void startGame(void)
{
    gScore = 0;
    gLives = 3;
    gStage = 0;
    gLoop  = 0;
    loadStage(0);
    gState = ST_READY;
    gStateT = 0;
    playSong(SONG_NONE);
}

/*  the camera. it chases the player at a quarter of the distance a
    frame, and the chase is measured the short way round so crossing
    the seam never sends it sprinting the length of the world.      */
static void camUpdate(void)
{
    float target = wrapW(gPlayer.x + AW * 0.5f - SCR_W * 0.5f);
    gCamX = wrapW(gCamX + wrapDX(gCamX, target) * CAM_LAG);
}

/* ------------------------------------------------------------------ */
/*  menus                                                              */
/* ------------------------------------------------------------------ */
#define TITLE_ROWS  2
#define PAUSE_ROWS  3
#define CFG_ROWS   (ACT_COUNT + 2)       /* actions, reset, back */

static int gCfgSel, gCfgCol, gCfgWait, gCfgFrom = ST_TITLE;

/*  menus answer to the bound keys and to the arrows either way, so a
    player who has just bound "up" to something silly can still get
    back out of the screen that let them do it.                     */
static int menuUp(void)
{
    return actHit(ACT_UP)   || gHit[VK_UP]   || (gPadDirEdge & PDIR_U);
}
static int menuDown(void)
{
    return actHit(ACT_DOWN) || gHit[VK_DOWN] || (gPadDirEdge & PDIR_D);
}
static int menuLeft(void)
{
    return actHit(ACT_LEFT) || gHit[VK_LEFT] || (gPadDirEdge & PDIR_L);
}
static int menuRight(void)
{
    return actHit(ACT_RIGHT) || gHit[VK_RIGHT] || (gPadDirEdge & PDIR_R);
}
static int menuOk(void)
{
    return actHit(ACT_JUMP) || gHit[VK_RETURN] || gHit[VK_SPACE] ||
           (gPadBtnEdge & (1u << PBTN_A));
}

static void enterPause(void)
{
    gState    = ST_PAUSE;
    gStateT   = 0;
    gPauseSel = 0;
    playSfx(SFX_PAUSE);       /* the blip first, then the score stops */
    audioPause(1);
}

static void leavePause(void)
{
    gState  = ST_PLAY;
    gStateT = 0;
    audioPause(0);
    playSfx(SFX_RESUME);
}

static void enterConfig(int from)
{
    gCfgFrom = from;
    gCfgSel  = 0;
    gCfgCol  = 0;
    gCfgWait = 0;
    gState   = ST_CONFIG;
    gStateT  = 0;
    playSfx(SFX_MENU);
}

static void leaveConfig(void)
{
    gState  = gCfgFrom;
    gStateT = 0;
    if (gCfgFrom == ST_TITLE) gMenuSel  = 1;
    else                      gPauseSel = 1;
    playSfx(SFX_MENU);
}

/*  one row of the table at a time. while gCfgWait is set the screen is
    listening instead of navigating, and the very next thing pressed -
    key or pad button, whichever column is selected - becomes the new
    binding. escape backs out of listening rather than out of the
    screen, which is what doBack below is checking for.              */
static void updateConfig(void)
{
    int i;

    if (gCfgWait) {
        if (gCfgCol < 2) {
            for (i = 1; i < 256; i++) {
                if (!gHit[i] || i == VK_ESCAPE || i == VK_F11) continue;
                gKeyBind[gCfgSel][gCfgCol] = (unsigned char)i;
                gCfgWait = 0;
                playSfx(SFX_BIND);
                return;
            }
        } else {
            for (i = 1; i < PBTN_COUNT; i++) {
                if (!(gPadBtnEdge & (1u << i))) continue;
                gPadBind[gCfgSel] = (unsigned char)i;
                gCfgWait = 0;
                playSfx(SFX_BIND);
                return;
            }
        }
        return;
    }

    if (menuUp())   { gCfgSel = (gCfgSel + CFG_ROWS - 1) % CFG_ROWS; playSfx(SFX_MENU); }
    if (menuDown()) { gCfgSel = (gCfgSel + 1) % CFG_ROWS;            playSfx(SFX_MENU); }
    if (gCfgSel < ACT_COUNT) {
        if (menuLeft()  && gCfgCol > 0) { gCfgCol--; playSfx(SFX_MENU); }
        if (menuRight() && gCfgCol < 2) { gCfgCol++; playSfx(SFX_MENU); }
    }
    if (menuOk()) {
        if (gCfgSel < ACT_COUNT)            { gCfgWait = 1; playSfx(SFX_MENU); }
        else if (gCfgSel == ACT_COUNT)      { bindDefaults(); playSfx(SFX_BIND); }
        else                                  leaveConfig();
    }
}

static void doBack(void)
{
    if (gFull) { toggleFullscreen(); return; }   /* leave fullscreen first */
    if (gState == ST_CONFIG) {
        if (gCfgWait) gCfgWait = 0;              /* stop listening */
        else          leaveConfig();
        return;
    }
    if (gState == ST_PAUSE) { leavePause(); return; }
    if (gState == ST_TITLE) { gRunning = 0; return; }
    audioPause(0);
    gState = ST_TITLE; gStateT = 0; playSong(SONG_TITLE);
}

static void update(void)
{
    int i;
    gFrame++;
    gStateT++;
    padUpdate();

    if (gShakeT > 0) gShakeT--;
    if (gHurtT  > 0) gHurtT--;

    {
        int handled = 1;

        switch (gState) {
        case ST_TITLE:
            if (menuUp())   { gMenuSel = (gMenuSel + TITLE_ROWS - 1) % TITLE_ROWS; playSfx(SFX_MENU); }
            if (menuDown()) { gMenuSel = (gMenuSel + 1) % TITLE_ROWS;              playSfx(SFX_MENU); }
            if (gStateT > 8 && menuOk()) {
                if (gMenuSel == 0) startGame();
                else               enterConfig(ST_TITLE);
            }
            break;
        case ST_CONFIG:
            updateConfig();
            break;
        case ST_PAUSE:
            if (gStateT > 12 && actHit(ACT_PAUSE)) { leavePause(); break; }
            if (menuUp())   { gPauseSel = (gPauseSel + PAUSE_ROWS - 1) % PAUSE_ROWS; playSfx(SFX_MENU); }
            if (menuDown()) { gPauseSel = (gPauseSel + 1) % PAUSE_ROWS;              playSfx(SFX_MENU); }
            if (gStateT > 12 && menuOk()) {
                switch (gPauseSel) {
                case 0:  leavePause(); break;
                case 1:  enterConfig(ST_PAUSE); break;
                default: audioPause(0);
                         gState = ST_TITLE; gStateT = 0; gMenuSel = 0;
                         playSong(SONG_TITLE);
                         break;
                }
            }
            break;
        case ST_PLAY:
            if (actHit(ACT_PAUSE)) enterPause();
            else handled = 0;
            break;
        default:
            handled = 0;
            break;
        }
        if (handled) { memset(gHit, 0, sizeof(gHit)); return; }
    }

    switch (gState) {
    case ST_READY:
        if (gStateT > 100) { gState = ST_PLAY; gStateT = 0; playSong(stageSong()); }
        break;

    case ST_PLAY:
        /*  hit stop: the whole field holds for a few frames when the
            player takes one, so the blow reads before the knock back
            starts moving.                                          */
        if (gFreeze > 0) { gFreeze--; break; }
        popUpdate();
        updatePlayer();
        if (gState != ST_PLAY) break;
        for (i = 0; i < gFoeCount; i++) updateFoe(&gFoe[i], i);
        if (gPlayer.invuln <= 0) {
            for (i = 0; i < gFoeCount; i++) {
                if (gFoe[i].stun > 0) continue;      /* dizzy = harmless */
                if (gPlayer.ride == i)  continue;    /* standing on it   */
                if (actorsHit(&gPlayer, &gFoe[i])) { hurtPlayer(gFoe[i].x); break; }
            }
        }
        if (gState != ST_PLAY) break;
        if (++gTimeTick >= 30) {
            gTimeTick = 0;
            if (--gTime <= 0) { gTime = 0; killPlayer(); }
        }
        break;

    case ST_DEAD:
        if (gStateT > 110) {
            gLives--;
            if (gLives <= 0) { gState = ST_OVER; gStateT = 0; playSong(SONG_OVER); }
            else { restartStage(); gState = ST_READY; gStateT = 0; }
        }
        break;

    case ST_CLEAR:
        /*  the clock runs a lot longer than it used to, so the bonus is
            counted off four units a frame instead of one every three. */
        if (gStateT < 110 && gTime > 0) {
            int n = 4;
            while (n-- > 0 && gTime > 0) { gTime--; gScore += 20; }
        }
        if (gStateT > 160) nextStage();
        break;

    case ST_ALLCLEAR:
        if (gStateT > 300) { loadStage(gStage); gState = ST_READY; gStateT = 0; }
        break;

    case ST_OVER:
        if (gStateT > 240 || menuOk()) {
            if (gScore > gHi) gHi = gScore;
            gState = ST_TITLE;
            gStateT = 0;
            gMenuSel = 0;
            playSong(SONG_TITLE);
        }
        break;
    }
    if (gState == ST_PLAY || gState == ST_READY ||
        gState == ST_DEAD || gState == ST_CLEAR) camUpdate();
    if (gScore > gHi) gHi = gScore;
    memset(gHit, 0, sizeof(gHit));
}

/* ------------------------------------------------------------------ */
/*  rendering                                                          */
/* ------------------------------------------------------------------ */
/*  the shake is folded into the camera and into the y the field is
    drawn at, so every world space draw picks it up for free.       */
static void viewBegin(void)
{
    gShakeX = 0;
    gDrawY0 = HUD_H;
    if (gShakeT > 0) {
        int a = (gShakeT + 1) / 2;
        gShakeX = (gFrame & 1) ? a : -a;
        gDrawY0 = HUD_H + (((gFrame >> 1) & 1) ? a : -a) / 2;
    }
}

static int fieldVisible(void)
{
    if (gState == ST_TITLE) return 0;
    if (gState == ST_CONFIG && gCfgFrom == ST_TITLE) return 0;
    return 1;
}

static void drawBackground(void)
{
    int y, i;
    fillRect(0, 0, SCR_W, HUD_H, 0x101018);
    for (y = 0; y < MAPH * TILE; y++) {
        unsigned t = 0x0C1030 + (unsigned)(y / 28) * 0x000103;
        fillRect(0, HUD_H + y, SCR_W, 1, t);
    }
    /*  the stars slide past at a third of the camera speed. on a bare
        stretch of deck they are the only thing telling the eye that
        the world is moving rather than the player.                  */
    for (i = 0; i < 90; i++) {
        int sx = ((i * 137 + 31) - (int)(gCamX * 0.34f)) % SCR_W;
        int sy = (i * 271 + 17) % (MAPH * TILE);
        unsigned c = ((gFrame / 12 + i) % 7 < 4) ? 0x30365A : 0x4A5280;
        if (sx < 0) sx += SCR_W;
        fillRect(sx, HUD_H + sy, 2, 2, c);
    }
}

/*  only the columns under the window are touched, and the column index
    is wrapped, so the seam draws itself without a special case.     */
static void drawMap(void)
{
    int r, i;
    int camI = (int)gCamX;
    int c0   = camI / TILE;
    int off  = camI - c0 * TILE;
    for (r = 0; r < MAPH; r++) {
        for (i = 0; i <= VIEWW; i++) {
            int c = wrapTX(c0 + i);
            int x = i * TILE - off + gShakeX;
            int y = gDrawY0 + r * TILE;
            switch (gmap[r][c]) {
            case '#': drawSprite(x, y, SPR_BLOCK,  0xB2743A, 0x6E4320, 0); break;
            case '=': drawSprite(x, y, SPR_PLAT,   0x9CC8E8, 0x4E7EA6, 0); break;
            case 'H': drawSprite(x, y, SPR_LADDER, 0xC08A40, 0x7A5020, 0); break;
            case 'o': drawSprite(x, y, SPR_APPLE,  0xE03A2E, 0x8C1A14, 0); break;
            case 'Q': drawSprite(x, y, SPR_CHERRY, 0xE03A2E, 0x8C1A14, 0); break;
            case '^': drawSprite(x, y, SPR_SPIKE,  0xD6DAE6, 0x8A8FA0, 0); break;
            case 'G':
                if (gFruit <= 0) drawSprite(x, y, SPR_HOUSE_OPEN, 0xF2D06B, 0xB08A20, 0);
                else             drawSprite(x, y, SPR_HOUSE,      0xF2D06B, 0xB08A20, 0);
                break;
            default: break;
            }
        }
    }
    /* blinking arrow over an open house */
    if (gFruit <= 0 && gGoalX >= 0 && (gFrame / 12) % 2 == 0) {
        int x = scrX((float)(gGoalX * TILE)) + 12, y = gDrawY0 + gGoalY * TILE - 12;
        fillRect(x, y, 8, 4, 0xFFE870);
        fillRect(x + 2, y + 4, 4, 4, 0xFFE870);
    }
}

/* the little birds that circle a dizzy foe */
static void drawDizzy(int cx, int cy)
{
    int k;
    for (k = 0; k < 3; k++) {
        double a = gFrame * 0.17 + k * 2.0944;
        int sx = cx + (int)(cos(a) * 14.0) - 3;
        int sy = cy + (int)(sin(a) * 5.0) - 2;
        int face = (cos(a) >= 0.0);
        fillRect(sx, sy, 6, 4, 0xFFE060);                    /* body */
        fillRect(sx + 1, sy + 2, 3, 1, 0xE0A830);            /* wing */
        fillRect(face ? sx + 4 : sx + 1, sy + 1, 1, 1, 0x201830);   /* eye  */
        fillRect(face ? sx + 6 : sx - 1, sy + 1, 1, 2, 0xF08A2B);   /* beak */
    }
}

static void drawActors(void)
{
    int i;
    Actor *p = &gPlayer;
    for (i = 0; i < gFoeCount; i++) {
        Actor *e = &gFoe[i];
        int kind = i % 3;
        int ex = scrX(e->x), ey = gDrawY0 + (int)e->y;
        if (ex < -TILE * 2 || ex > SCR_W + TILE) continue;
        if (e->stun > 0) {
            /*  the sheet draws its own flattened pose, so a kicked foe
                is a frame now rather than a squashed copy of a standing
                one.                                                  */
            drawArt(ex, ey, FOE_FLAT[kind], e->dir < 0);
            drawDizzy(ex + AW / 2, ey + 6);
        } else {
            drawArt(ex, ey, FOE_WALK[kind][(e->anim / 7) & 1], e->dir < 0);
        }
    }
    if (gState == ST_DEAD) {
        int t = gStateT;
        int off = (t < 55) ? -(t * t) / 26 : -(55 * 55) / 26 + ((t - 55) * (t - 55)) / 16;
        if (((t / 4) & 1) || t > 30)
            drawArt(scrX(p->x), gDrawY0 + (int)p->y + off, AF_HURT, 0);
        return;
    }
    if (!p->alive) return;
    if (p->invuln > 0 && (gFrame / 4) % 2) return;      /* blink while safe */
    {
        int px = scrX(p->x), py = gDrawY0 + (int)p->y;
        int id;
        /*  the sheet has a six frame walk, and a rise and a fall that
            are two different poses, so the animation follows what the
            player is actually doing rather than a two frame flip.   */
        if (p->climb)            id = ((p->anim / 6) & 1) ? AF_W0 : AF_W3;
        else if (!p->onground)   id = (p->vy < 0) ? AF_JUMP : AF_FALL;
        else if (p->vx != 0)     id = PLR_WALK[(p->anim / 5) % 6];
        else                     id = AF_IDLE;
        drawArt(px, py, id, p->dir < 0);
        /*  the foot goes over the body - it is a leg thrust out in
            front, not something happening behind it. it sweeps first,
            then lands with the burst on it.                         */
        if (p->kick > KICK_DUR - KICK_ACT)
            drawArt(px, py, (p->kick > KICK_DUR - 4) ? AF_KICK0 : AF_KICK1,
                    p->dir < 0);
    }
}

/*  taking a hit: a white snap for two frames and then a red wash that
    fades out over the mercy blink, on top of the shake and the sparks
    that hurtFx already threw.                                       */
static void drawHurtFx(void)
{
    float f;
    if (gHurtT <= 0) return;
    f = gHurtT / (float)HURT_FX;
    if (gHurtT > HURT_FX - 2)
        blendRect(0, HUD_H, SCR_W, MAPH * TILE, 0xFFFFFF, 0.34f);
    blendRect(0, HUD_H, SCR_W, MAPH * TILE, 0xFF2A4A, f * f * 0.45f);
    /* and a bright frame round the edge of the field */
    blendRect(0, HUD_H, SCR_W, 6, 0xFF4060, f * 0.8f);
    blendRect(0, SCR_H - 6, SCR_W, 6, 0xFF4060, f * 0.8f);
    blendRect(0, HUD_H, 6, MAPH * TILE, 0xFF4060, f * 0.8f);
    blendRect(SCR_W - 6, HUD_H, 6, MAPH * TILE, 0xFF4060, f * 0.8f);
}

/*  score popups. the last third of the life fades the text down into
    the night sky background rather than snapping it off, and a chained
    pickup gets the COMBO line with the multiplier that was applied.   */
static void drawPopups(void)
{
    int i;
    for (i = 0; i < MAXPOP; i++) {
        Popup *q = &gPop[i];
        char  buf[48];
        int   x, y;
        float fade = (q->life > POP_LIFE / 3) ? 1.0f : q->life / (POP_LIFE / 3.0f);
        unsigned base, col;
        if (q->life <= 0) continue;
        x = scrX(q->x) - 60;                /* centred in its own 120px box */
        if (x < -120 || x > SCR_W) continue;
        y = gDrawY0 + (int)q->y;
        if (y < HUD_H + 17) y = HUD_H + 17;  /* keep the combo line clear of the hud */
        base = (q->mult > 1.0f) ? 0xFFD060 : 0xFFFFFF;
        col  = ((unsigned)(((base >> 16) & 255) * fade) << 16) |
               ((unsigned)(((base >>  8) & 255) * fade) <<  8) |
                (unsigned)(( base        & 255) * fade);
        if (q->mult > 1.0f) {
            sprintf(buf, "COMBO x%.1f", q->mult);
            drawTextBox(buf, x, y - 15, 120, col);
        }
        sprintf(buf, "%d", q->score);
        drawTextBox(buf, x, y, 120, col);
    }
}

static void drawHeart(int x, int y, int full)
{
    unsigned c = full ? 0xF04868 : 0x3A2C34;
    unsigned h = full ? 0xFF9AB0 : 0x4A3A42;
    fillRect(x + 1, y,     4, 4, c);
    fillRect(x + 7, y,     4, 4, c);
    fillRect(x,     y + 3, 12, 4, c);
    fillRect(x + 1, y + 7, 10, 2, c);
    fillRect(x + 3, y + 9,  6, 2, c);
    fillRect(x + 5, y + 11, 2, 1, c);
    fillRect(x + 2, y + 2,  2, 2, h);
}

/*  the japanese labels are wider than the old ascii ones and the face
    actually used is decided at runtime, so lay the bar out from the
    measured width of each field instead of hard coded columns.       */
static void drawHud(void)
{
    char buf[128];
    int i, x = 8, w;

    sprintf(buf, "スコア %06d", gScore);
    drawText(buf, x, 10, 0xFFFFFF, 0, 0);
    x += textWidth(buf, 0) + 14;

    sprintf(buf, "ハイ %06d", gHi);
    drawText(buf, x, 10, 0xFFD060, 0, 0);
    x += textWidth(buf, 0) + 14;

    sprintf(buf, "ステージ %d-%d", gLoop + 1, gStage + 1);
    drawText(buf, x, 10, 0x9CE0FF, 0, 0);
    x += textWidth(buf, 0) + 14;

    sprintf(buf, "フルーツ %02d", gFruit);
    drawText(buf, x, 10, (gFruit <= 0) ? 0x70E090 : 0xFFFFFF, 0, 0);
    x += textWidth(buf, 0) + 14;

    sprintf(buf, "タイム %03d", gTime);
    drawText(buf, x, 10, (gTime < 30 && (gFrame / 8) % 2) ? 0xFF5050 : 0xFFFFFF, 0, 0);

    /* hearts and spare balls are pinned to the right hand end */
    sprintf(buf, "×%d", gLives > 0 ? gLives - 1 : 0);
    w = textWidth(buf, 0);
    drawText(buf, SCR_W - 10 - w, 10, 0xFFFFFF, 0, 0);
    GdiFlush();

    x = SCR_W - 10 - w - 8 - 14;
    fillRect(x,     15, 12, 10, PC_MAIN);       /* spare ball marker */
    fillRect(x + 3, 13,  6, 14, PC_MAIN);
    fillRect(x + 3, 17,  3,  3, 0xFFFFFF);
    x -= 12 + LIFE_MAX * 16;
    for (i = 0; i < LIFE_MAX; i++)              /* hearts left this stage */
        drawHeart(x + i * 16, 13, i < gLife);
}

/* knock the already drawn frame back so text reads over it */
static void dimScreen(void)
{
    int i, n = SCR_W * SCR_H;
    for (i = 0; i < n; i++) gPix[i] = (gPix[i] >> 2) & 0x3F3F3F;
}

/*  a dark slab behind a menu. dimScreen alone leaves the ladders and
    the platforms showing through the table, which is hard to read. */
static void menuPanel(int x, int y, int w, int h)
{
    blendRect(x, y, w, h, 0x060810, 0.80f);
    blendRect(x, y, w, 2, 0x4A5878, 0.85f);
    blendRect(x, y + h - 2, w, 2, 0x4A5878, 0.85f);
    blendRect(x, y, 2, h, 0x4A5878, 0.85f);
    blendRect(x + w - 2, y, 2, h, 0x4A5878, 0.85f);
}

/* the little pointer that marks the row a menu is sitting on */
static void menuMark(int x, int y)
{
    if ((gFrame / 8) % 2) return;
    fillRect(x,     y + 2, 3, 12, 0xFFD060);
    fillRect(x + 3, y + 4, 3,  8, 0xFFD060);
    fillRect(x + 6, y + 6, 3,  4, 0xFFD060);
}

/*  what the pad column says for one action. the four directions always
    answer to the stick and the d-pad, so an empty binding there is not
    the same thing as no control at all.                             */
static const char *padLabel(int a)
{
    if (gPadBind[a]) return PBTN_NAME[gPadBind[a]];
    return (a <= ACT_DOWN) ? "スティック" : "--";
}


/*  the pause menu. the pause button itself always drops straight back
    into the game; the list is for everything else.                  */
static void drawPause(void)
{
    static const char *items[PAUSE_ROWS] = {
        "つづける", "キーコンフィグ", "タイトルへ"
    };
    int i, y = HUD_H + 210;

    dimScreen();
    /*  one row shorter than it was, so the panel closes up with it */
    menuPanel(SCR_W / 2 - 230, HUD_H + 96, 460, 262);
    drawText("ポーズちゅう", 0, HUD_H + 120, 0xFFD060, 1, 1);
    for (i = 0; i < PAUSE_ROWS; i++) {
        unsigned col = (i == gPauseSel) ? 0xFFFFFF : 0x8090B0;
        drawText(items[i], 0, y + i * 30, col, 0, 1);
    }
    GdiFlush();
    menuMark(SCR_W / 2 - 130, y + gPauseSel * 30);

    drawText("ポーズボタン か ESC で すぐ さいかい",
             0, HUD_H + 320, 0x7AA8D0, 0, 1);
    GdiFlush();
}

/*  one cell of the config table. the highlight is painted straight
    into the buffer before the text, which GDI then draws on top of
    when it flushes.                                                */
static void cfgCell(int x, int y, const char *txt, int sel)
{
    if (sel) {
        int w = textWidth(txt, 0);
        if (w < 46) w = 46;
        GdiFlush();
        fillRect(x - 7, y - 2, w + 14, 22, gCfgWait ? 0x7A3A18 : 0x2E4468);
    }
    drawText(txt, x, y, sel ? 0xFFE070 : 0xC8D0E0, 0, 0);
}

static void drawConfig(void)
{
    int a, y, ry;
    const int x0 = 150, x1 = 380, x2 = 550, x3 = 730;

    if (fieldVisible()) dimScreen();
    menuPanel(100, HUD_H + 4, SCR_W - 200, 496);
    drawText("キーコンフィグ", 0, HUD_H + 10, 0xFFD060, 1, 1);

    y = HUD_H + 62;
    drawText("そうさ",     x0, y, 0xFFD060, 0, 0);
    drawText("キー 1",     x1, y, 0xFFD060, 0, 0);
    drawText("キー 2",     x2, y, 0xFFD060, 0, 0);
    drawText("パッド",     x3, y, 0xFFD060, 0, 0);

    for (a = 0; a < ACT_COUNT; a++) {
        int sel = (gCfgSel == a);
        ry = y + 30 + a * 26;
        drawText(ACT_LABEL[a], x0, ry, sel ? 0xFFFFFF : 0x9CE0FF, 0, 0);
        cfgCell(x1, ry, keyName(gKeyBind[a][0]), sel && gCfgCol == 0);
        cfgCell(x2, ry, keyName(gKeyBind[a][1]), sel && gCfgCol == 1);
        cfgCell(x3, ry, padLabel(a),             sel && gCfgCol == 2);
    }

    ry = y + 30 + ACT_COUNT * 26 + 16;
    drawText("さいしょの せっていに もどす", x0, ry,
             (gCfgSel == ACT_COUNT) ? 0xFFE070 : 0x9CE0FF, 0, 0);
    drawText("とじる", x0, ry + 26,
             (gCfgSel == ACT_COUNT + 1) ? 0xFFE070 : 0x9CE0FF, 0, 0);

    /*  the keys the window itself answers to. they cannot be moved, so
        they sit below the table as a plain list rather than a row that
        the cursor can land on - and this is the only place they are
        written down now that the instructions page is gone.        */
    drawText("かえられない キー", x0, ry + 56, 0xFFD060, 0, 0);
    drawText("ぜんがめん",   x0, ry + 80,  0x9CE0FF, 0, 0);
    drawText("F11",          x1, ry + 80,  0xFFFFFF, 0, 0);
    drawText("がめんサイズ", x0, ry + 102, 0x9CE0FF, 0, 0);
    drawText("1 - 4",        x1, ry + 102, 0xFFFFFF, 0, 0);
    GdiFlush();
    menuMark(x0 - 26, y + 30 + (gCfgSel < ACT_COUNT ? gCfgSel * 26
                                : ACT_COUNT * 26 + 16 + (gCfgSel - ACT_COUNT) * 26));

    if (gCfgWait)
        drawText("わりあてたい キー か パッドの ボタンを おして ください",
                 0, HUD_H + 452, 0xFFD060, 0, 1);
    else
        drawText("やじるし で えらぶ   ジャンプボタン で へんこう",
                 0, HUD_H + 452, 0x7AA8D0, 0, 1);
    drawText("ESC で もどる   ひだり みぎ で キー1 キー2 パッド を えらぶ",
             0, HUD_H + 476, 0x7AA8D0, 0, 1);
    GdiFlush();
}

static void drawOverlay(void)
{
    char buf[128];
    switch (gState) {
    case ST_TITLE: {
        static const char *items[TITLE_ROWS] = {
            "ゲーム スタート", "キーコンフィグ"
        };
        int i;
        for (i = 0; i < 8; i++) {
            int x = 50 + i * 118, y = HUD_H + 410 + (int)(14 * sin((gFrame + i * 20) * 0.06));
            if (i & 1) drawArt(x, y, FOE_WALK[0][(gFrame / 7 + i) & 1], 1);
            else       drawArt(x, y, PLR_WALK[(gFrame / 5 + i) % 6], 0);
        }
        /*  four screen pixels to the dot puts the logo at 608x120,
            which leaves a clear thirty above the first menu row.   */
        drawLogo(SCR_W / 2, HUD_H + 42, 4);
        for (i = 0; i < TITLE_ROWS; i++)
            drawText(items[i], 0, HUD_H + 190 + i * 32,
                     (i == gMenuSel) ? 0xFFFFFF : 0x8090B0, 0, 1);
        if (gPadType == PAD_XINPUT)     drawText("ゲームパッド ： xinput",   0, HUD_H + 330, 0x4E7A5E, 0, 1);
        else if (gPadType == PAD_JOY)   drawText("ゲームパッド ： joystick", 0, HUD_H + 330, 0x4E7A5E, 0, 1);
        else                            drawText("キーボードのみ",           0, HUD_H + 330, 0x4A5060, 0, 1);
        GdiFlush();
        menuMark(SCR_W / 2 - 130, HUD_H + 190 + gMenuSel * 32);
        break; }
    case ST_PAUSE:
        drawPause();
        break;
    case ST_CONFIG:
        drawConfig();
        break;
    case ST_READY:
        sprintf(buf, "ステージ %d-%d", gLoop + 1, gStage + 1);
        drawText(buf, 0, HUD_H + 170, 0xFFFFFF, 1, 1);
        drawText("よういは いいか？", 0, HUD_H + 220, 0xFFD060, 1, 1);
        GdiFlush();
        break;
    case ST_DEAD:
        if (gStateT > 40) drawText("いたっ！", 0, HUD_H + 190, 0xFF6060, 1, 1);
        GdiFlush();
        break;
    case ST_CLEAR:
        drawText("ステージ クリア！", 0, HUD_H + 160, 0xFFD060, 1, 1);
        sprintf(buf, "タイム ボーナス  %d", gTime * 20);
        drawText(buf, 0, HUD_H + 210, 0xFFFFFF, 0, 1);
        GdiFlush();
        break;
    case ST_ALLCLEAR:
        drawText("ぜんステージ クリア！", 0, HUD_H + 150, 0xFFD060, 1, 1);
        sprintf(buf, "ラウンド %d かいし  てきが はやくなる", gLoop + 1);
        drawText(buf, 0, HUD_H + 210, 0xFFFFFF, 0, 1);
        GdiFlush();
        break;
    case ST_OVER:
        drawText("ゲームオーバー", 0, HUD_H + 180, 0xFF6060, 1, 1);
        sprintf(buf, "スコア %06d", gScore);
        drawText(buf, 0, HUD_H + 230, 0xFFFFFF, 0, 1);
        GdiFlush();
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  display : window scaling, letterbox, fullscreen                    */
/* ------------------------------------------------------------------ */
#define WIN_STYLE (WS_OVERLAPPEDWINDOW)
#define MIN_CLI_W (SCR_W / 2)
#define MIN_CLI_H (SCR_H / 2)

static int   gCliW = SCR_W, gCliH = SCR_H;
static RECT  gDst = { 0, 0, SCR_W, SCR_H };
static WINDOWPLACEMENT gWP;

/*  the back buffer keeps its native 640x488; it is blown up to the
    client area with the largest whole-number factor that fits, so the
    pixels stay square and crisp. leftovers become black bars.        */
static void computeDst(void)
{
    RECT rc;
    int w, h, si, sj, s;
    if (!gWnd) return;
    GetClientRect(gWnd, &rc);
    gCliW = rc.right  > 1 ? rc.right  : 1;
    gCliH = rc.bottom > 1 ? rc.bottom : 1;

    si = gCliW / SCR_W;
    sj = gCliH / SCR_H;
    s  = (si < sj) ? si : sj;
    if (s >= 1) {
        w = SCR_W * s;
        h = SCR_H * s;
    } else {                                   /* smaller than 1x: fit */
        double fx = (double)gCliW / SCR_W;
        double fy = (double)gCliH / SCR_H;
        double f  = (fx < fy) ? fx : fy;
        w = (int)(SCR_W * f); if (w < 1) w = 1;
        h = (int)(SCR_H * f); if (h < 1) h = 1;
    }
    gDst.left   = (gCliW - w) / 2;
    gDst.top    = (gCliH - h) / 2;
    gDst.right  = gDst.left + w;
    gDst.bottom = gDst.top  + h;
}

static void blitTo(HDC dc)
{
    int x = gDst.left, y = gDst.top;
    int w = gDst.right - gDst.left, h = gDst.bottom - gDst.top;
    if (x > 0) {
        PatBlt(dc, 0, 0, x, gCliH, BLACKNESS);
        PatBlt(dc, x + w, 0, gCliW - (x + w), gCliH, BLACKNESS);
    }
    if (y > 0) {
        PatBlt(dc, x, 0, w, y, BLACKNESS);
        PatBlt(dc, x, y + h, w, gCliH - (y + h), BLACKNESS);
    }
    if (w == SCR_W && h == SCR_H) {
        BitBlt(dc, x, y, SCR_W, SCR_H, gMemDC, 0, 0, SRCCOPY);
    } else {
        SetStretchBltMode(dc, COLORONCOLOR);   /* nearest neighbour */
        StretchBlt(dc, x, y, w, h, gMemDC, 0, 0, SCR_W, SCR_H, SRCCOPY);
    }
}

static void setWindowScale(int s)
{
    RECT rc;
    MONITORINFO mi;
    int w, h, x, y;
    if (gFull || !gWnd) return;
    if (s < 1) s = 1;
    rc.left = 0; rc.top = 0; rc.right = SCR_W * s; rc.bottom = SCR_H * s;
    AdjustWindowRect(&rc, WIN_STYLE, FALSE);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoA(MonitorFromWindow(gWnd, MONITOR_DEFAULTTONEAREST), &mi);
    x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    y = mi.rcWork.top  + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top)  y = mi.rcWork.top;
    ShowWindow(gWnd, SW_RESTORE);
    SetWindowPos(gWnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    computeDst();
}

/* biggest whole-number scale that leaves the window inside the desktop */
static int bestStartScale(void)
{
    MONITORINFO mi;
    int aw, ah, s;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(MonitorFromWindow(gWnd, MONITOR_DEFAULTTONEAREST), &mi)) return 1;
    aw = (int)((mi.rcWork.right - mi.rcWork.left) * 0.90);
    ah = (int)((mi.rcWork.bottom - mi.rcWork.top) * 0.90);
    for (s = 4; s > 1; s--)
        if (SCR_W * s <= aw && SCR_H * s <= ah) return s;
    return 1;
}

static void toggleFullscreen(void)
{
    MONITORINFO mi;
    if (!gWnd) return;
    if (!gFull) {
        gWP.length = sizeof(gWP);
        GetWindowPlacement(gWnd, &gWP);
        mi.cbSize = sizeof(mi);
        GetMonitorInfoA(MonitorFromWindow(gWnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrA(gWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(gWnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        gFull = 1;
    } else {
        SetWindowLongPtrA(gWnd, GWL_STYLE, WIN_STYLE | WS_VISIBLE);
        SetWindowPlacement(gWnd, &gWP);
        SetWindowPos(gWnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        gFull = 0;
    }
    computeDst();
}

static void render(void)
{
    HDC dc;
    int live = (gState == ST_PLAY || gState == ST_PAUSE || gState == ST_CONFIG);
    viewBegin();
    drawBackground();
    if (fieldVisible()) {
        drawMap();
        /*  effects only run while the field does - a frozen trail hanging
            over the death or the stage clear animation reads as a bug. */
        if (live) drawDashFx();
        drawActors();
        if (live) drawComboGauge();
        drawPopups();
        GdiFlush();            /* the wash below has to see the text */
        drawHurtFx();
        drawHud();
    }
    drawOverlay();
    if (gPadNotice > 0 && (gFrame / 6) % 2)
        drawText("ゲームパッド を みつけた", 0, SCR_H - 26, 0x70E090, 0, 1);
    GdiFlush();
    dc = GetDC(gWnd);
    blitTo(dc);
    ReleaseDC(gWnd, dc);
}

/* ------------------------------------------------------------------ */
/*  window                                                             */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_DESTROY: gRunning = 0; PostQuitMessage(0); return 0;
    case WM_CLOSE:   gRunning = 0; return 0;
    case WM_KEYDOWN:
        if (w < 256) { if (!gKey[w]) gHit[w] = 1; gKey[w] = 1; }
        if (w == VK_ESCAPE) doBack();
        if (w == VK_F11)    toggleFullscreen();
        if (w >= '1' && w <= '4') setWindowScale((int)w - '0');
        return 0;
    case WM_SYSKEYDOWN:
        if (w == VK_RETURN && (l & (1 << 29))) { toggleFullscreen(); return 0; }
        break;
    case WM_KEYUP:
        if (w < 256) gKey[w] = 0;
        return 0;
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) computeDst();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)l;
        RECT rc;
        rc.left = 0; rc.top = 0; rc.right = MIN_CLI_W; rc.bottom = MIN_CLI_H;
        AdjustWindowRect(&rc, WIN_STYLE, FALSE);
        mm->ptMinTrackSize.x = rc.right - rc.left;
        mm->ptMinTrackSize.y = rc.bottom - rc.top;
        return 0; }
    case WM_SETCURSOR:
        if (gFull && LOWORD(l) == HTCLIENT) { SetCursor(NULL); return TRUE; }
        break;
    case WM_DEVICECHANGE:
        gPadScan = 1;          /* a device appeared or left - look again now */
        return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        blitTo(dc);
        EndPaint(h, &ps);
        return 0; }
    }
    return DefWindowProc(h, m, w, l);
}

static void createBuffer(void)
{
    BITMAPINFO bi;
    HDC dc = GetDC(NULL);
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = SCR_W;
    bi.bmiHeader.biHeight      = -SCR_H;      /* top down */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    gMemDC = CreateCompatibleDC(dc);
    gDIB   = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&gPix, NULL, 0);
    SelectObject(gMemDC, gDIB);
    ReleaseDC(NULL, dc);

    pickFace();
    gFontBig = CreateFontW(34, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           NONANTIALIASED_QUALITY, FF_DONTCARE, gFace);
    gFontSml = CreateFontW(18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           NONANTIALIASED_QUALITY, FF_DONTCARE, gFace);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cl, int sc)
{
    WNDCLASSA wc;
    RECT rc;
    MSG msg;
    LARGE_INTEGER freq, prev, now;
    double acc = 0.0, step;
    int stepped;

    (void)hp; (void)cl;

    /* real pixels on scaled desktops (no blurry DWM upscale) */
    {
        typedef BOOL (WINAPI *DPIAWARE)(void);
        HMODULE u = GetModuleHandleA("user32.dll");
        DPIAWARE fn = u ? (DPIAWARE)GetProcAddress(u, "SetProcessDPIAware") : NULL;
        if (fn) fn();
    }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "EggnSoy";
    RegisterClassA(&wc);

    rc.left = 0; rc.top = 0; rc.right = SCR_W; rc.bottom = SCR_H;
    AdjustWindowRect(&rc, WIN_STYLE, FALSE);
    gWnd = CreateWindowA("EggnSoy", "EGG n SOY",
                         WIN_STYLE,
                         CW_USEDEFAULT, CW_USEDEFAULT,
                         rc.right - rc.left, rc.bottom - rc.top,
                         NULL, NULL, hi, NULL);
    if (!gWnd) return 1;
    ShowWindow(gWnd, sc);
    UpdateWindow(gWnd);
    setWindowScale(bestStartScale());
    computeDst();

    timeBeginPeriod(1);        /* keep Sleep(1) honest -> low input lag */
    bindDefaults();
    createBuffer();
    artInit();
    audioInit();
    padInit();
    padDetect();
    srand(GetTickCount());

    loadStage(0);
    gState = ST_TITLE;
    gLives = 3;
    playSong(SONG_TITLE);

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);
    step = 1.0 / 60.0;

    while (gRunning) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) gRunning = 0;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        QueryPerformanceCounter(&now);
        acc += (double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart;
        prev = now;
        if (acc > 0.25) acc = 0.25;
        stepped = 0;
        while (acc >= step) { update(); acc -= step; stepped = 1; }
        if (stepped) render();      /* no point redrawing an unchanged frame */
        Sleep(1);
    }

    audioShutdown();
    padShutdown();
    timeEndPeriod(1);
    DeleteObject(gDIB);
    DeleteObject(gFontBig);
    DeleteObject(gFontSml);
    DeleteDC(gMemDC);
    DestroyWindow(gWnd);
    return 0;
}
