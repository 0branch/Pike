/*
|| This file is part of Pike. For copyright information see COPYRIGHT.
|| Pike is distributed under GPL, LGPL and MPL. See the file COPYING
|| for more information.
|| $Id: stuff.c,v 1.23 2004/11/14 18:03:50 mast Exp $
*/

#include "global.h"
#include "stuff.h"
#include "stralloc.h"

/* Used by is8bitalnum in pike_macros.h. */
PMOD_EXPORT const char Pike_is8bitalnum_vector[] =
  "0000000000000000"
  "0000000000000000"
  "0000000000000000"
  "1111111111000000"
  "0111111111111111"
  "1111111111100001"
  "0111111111111111"
  "1111111111100000"
  "0000000000000000"
  "0000000000000000"
  "1011110101100010"
  "1011011001101110"
  "1111111111111111"
  "1111111011111111"
  "1111111111111111"
  "1111111011111111";

/* Not all of these are primes, but they should be adequate */
PMOD_EXPORT const INT32 hashprimes[32] =
{
  31,        /* ~ 2^0  = 1 */
  31,        /* ~ 2^1  = 2 */
  31,        /* ~ 2^2  = 4 */
  31,        /* ~ 2^3  = 8 */
  31,        /* ~ 2^4  = 16 */
  31,        /* ~ 2^5  = 32 */
  61,        /* ~ 2^6  = 64 */
  127,       /* ~ 2^7  = 128 */
  251,       /* ~ 2^8  = 256 */
  541,       /* ~ 2^9  = 512 */
  1151,      /* ~ 2^10 = 1024 */
  2111,      /* ~ 2^11 = 2048 */
  4327,      /* ~ 2^12 = 4096 */
  8803,      /* ~ 2^13 = 8192 */
  17903,     /* ~ 2^14 = 16384 */
  32321,     /* ~ 2^15 = 32768 */
  65599,     /* ~ 2^16 = 65536 */
  133153,    /* ~ 2^17 = 131072 */
  270001,    /* ~ 2^18 = 264144 */
  547453,    /* ~ 2^19 = 524288 */
  1109891,   /* ~ 2^20 = 1048576 */
  2000143,   /* ~ 2^21 = 2097152 */
  4561877,   /* ~ 2^22 = 4194304 */
  9248339,   /* ~ 2^23 = 8388608 */
  16777215,  /* ~ 2^24 = 16777216 */
  33554431,  /* ~ 2^25 = 33554432 */
  67108863,  /* ~ 2^26 = 67108864 */
  134217727, /* ~ 2^27 = 134217728 */
  268435455, /* ~ 2^28 = 268435456 */
  536870911, /* ~ 2^29 = 536870912 */
  1073741823,/* ~ 2^30 = 1073741824 */
  2147483647,/* ~ 2^31 = 2147483648 */
};

/* same thing as (int)floor(log((double)x) / log(2.0)) */
/* Except a bit quicker :) (hopefully) */

PMOD_EXPORT int my_log2(size_t x)
{
  static const signed char bit[256] =
  {
    -1, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
     5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
     5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
  };
  register size_t tmp;
#if SIZEOF_CHAR_P > 4
  if((tmp=(x>>32)))
  {
    if((x=(tmp>>16))) {
      if((tmp=(x>>8))) return bit[tmp]+56;
      return bit[x]+48;
    }
    if((x=(tmp>>8))) return bit[x]+40;
    return bit[tmp]+32;
  }
#endif /* SIZEOF_CHAP_P > 4 */
  if((tmp=(x>>16)))
  {
    if((x=(tmp>>8))) return bit[x]+24;
    return bit[tmp]+16;
  }
  if((tmp=(x>>8))) return bit[tmp]+8;
  return bit[x];
}


/* Return the number of bits in a 32-bit integer */
PMOD_EXPORT int count_bits(unsigned INT32 x)
{
#define B(X) X+0,X+1,X+1,X+2,\
             X+1,X+2,X+2,X+3,\
             X+1,X+2,X+2,X+3,\
             X+2,X+3,X+3,X+4
  static const char bits[256] =
  {
    B(0), B(1), B(1), B(2),
    B(1), B(2), B(2), B(3),
    B(1), B(2), B(2), B(3),
    B(2), B(3), B(3), B(4)
  };

  return (bits[x & 255] +
	  bits[(x>>8) & 255] +
	  bits[(x>>16) & 255] +
	  bits[(x>>24) & 255]);
}

/* Return true for integers with more than one bit set */
PMOD_EXPORT int is_more_than_one_bit(unsigned INT32 x)
{
  return !!(x & (x-1));
}

PMOD_EXPORT double my_strtod(char *nptr, char **endptr)
{
  double tmp=STRTOD(nptr,endptr);
  if(*endptr>nptr)
  {
    if(endptr[0][-1]=='.')
      endptr[0]--;
  }
  return tmp;
}

PMOD_EXPORT unsigned INT32 my_sqrt(unsigned INT32 n)
{
  unsigned INT32 b, s, y=0;
  unsigned INT16 x=0;

  for(b=1<<(sizeof(INT32)*8-2); b; b>>=2)
  {
    x<<=1; s=b+y; y>>=1;
    if(n>=s)
    {
      x|=1; y|=b; n-=s;
    }
  }
  return x;
}

/* This routine basically finds a prime number
 * which is larger than 'x'
 */
unsigned long find_good_hash_size(unsigned long num)
{
  static const unsigned long primes[] =
  {
    37UL,         37UL,         37UL,         37UL,
    37UL,         37UL,         41UL,         41UL,
    41UL,         41UL,         43UL,         43UL,
    47UL,         47UL,         47UL,         47UL,
    53UL,         53UL,         53UL,         53UL,
    53UL,         53UL,         59UL,         59UL,
    59UL,         59UL,         59UL,         59UL,
    61UL,         61UL,         67UL,         67UL,
    67UL,         67UL,         71UL,         71UL,
    73UL,         79UL,         79UL,         79UL,
    83UL,         83UL,         89UL,         89UL,
    89UL,         97UL,         97UL,         97UL,
    97UL,         101UL,        101UL,        103UL,
    107UL,        107UL,        109UL,        113UL,
    113UL,        127UL,        127UL,        127UL,
    127UL,        127UL,        127UL,        127UL,
    131UL,        137UL,        139UL,        149UL,
    149UL,        151UL,        157UL,        163UL,
    163UL,        167UL,        173UL,        179UL,
    179UL,        191UL,        191UL,        191UL,
    197UL,        199UL,        211UL,        211UL,
    211UL,        223UL,        223UL,        223UL,
    227UL,        233UL,        239UL,        239UL,
    251UL,        251UL,        251UL,        257UL,
    263UL,        271UL,        281UL,        293UL,
    307UL,        307UL,        311UL,        331UL,
    331UL,        337UL,        347UL,        353UL,
    359UL,        367UL,        379UL,        383UL,
    397UL,        401UL,        409UL,        419UL,
    431UL,        431UL,        439UL,        449UL,
    457UL,        463UL,        479UL,        479UL,
    487UL,        499UL,        503UL,        521UL,
    541UL,        547UL,        563UL,        577UL,
    593UL,        607UL,        631UL,        641UL,
    659UL,        673UL,        691UL,        709UL,
    719UL,        739UL,        751UL,        769UL,
    787UL,        809UL,        821UL,        839UL,
    853UL,        863UL,        881UL,        907UL,
    911UL,        929UL,        947UL,        967UL,
    977UL,        991UL,        1009UL,       1031UL,
    1061UL,       1087UL,       1123UL,       1151UL,
    1187UL,       1217UL,       1249UL,       1279UL,
    1319UL,       1361UL,       1381UL,       1409UL,
    1439UL,       1471UL,       1511UL,       1543UL,
    1567UL,       1601UL,       1637UL,       1663UL,
    1697UL,       1733UL,       1759UL,       1801UL,
    1823UL,       1861UL,       1889UL,       1931UL,
    1951UL,       1987UL,       2017UL,       2053UL,
    2111UL,       2179UL,       2239UL,       2309UL,
    2371UL,       2437UL,       2503UL,       2579UL,
    2633UL,       2687UL,       2753UL,       2819UL,
    2879UL,       2953UL,       3011UL,       3079UL,
    3137UL,       3203UL,       3271UL,       3329UL,
    3391UL,       3457UL,       3527UL,       3583UL,
    3659UL,       3719UL,       3779UL,       3847UL,
    3907UL,       3967UL,       4049UL,       4099UL,
    4229UL,       4357UL,       4481UL,       4621UL,
    4751UL,       4871UL,       4993UL,       5119UL,
    5261UL,       5381UL,       5503UL,       5639UL,
    5779UL,       5897UL,       6029UL,       6143UL,
    6271UL,       6421UL,       6529UL,       6659UL,
    6791UL,       6911UL,       7039UL,       7177UL,
    7297UL,       7433UL,       7559UL,       7681UL,
    7817UL,       7937UL,       8069UL,       8191UL,
    8447UL,       8707UL,       8963UL,       9221UL,
    9473UL,       9733UL,       10007UL,      10243UL,
    10499UL,      10753UL,      11027UL,      11273UL,
    11519UL,      11777UL,      12037UL,      12289UL,
    12547UL,      12799UL,      13063UL,      13313UL,
    13567UL,      13829UL,      14081UL,      14341UL,
    14591UL,      14851UL,      15107UL,      15359UL,
    15619UL,      15877UL,      16127UL,      16411UL,
    16901UL,      17417UL,      17921UL,      18433UL,
    18947UL,      19457UL,      19973UL,      20479UL,
    21001UL,      21503UL,      22027UL,      22531UL,
    23039UL,      23557UL,      24071UL,      24593UL,
    25087UL,      25601UL,      26111UL,      26627UL,
    27143UL,      27647UL,      28163UL,      28687UL,
    29191UL,      29717UL,      30211UL,      30727UL,
    31231UL,      31751UL,      32257UL,      32771UL,
    33791UL,      34819UL,      35839UL,      36871UL,
    37889UL,      38917UL,      39937UL,      40961UL,
    41983UL,      43013UL,      44041UL,      45061UL,
    46091UL,      47111UL,      48131UL,      49157UL,
    50177UL,      51199UL,      52223UL,      53267UL,
    54277UL,      55313UL,      56333UL,      57347UL,
    58367UL,      59393UL,      60427UL,      61441UL,
    62467UL,      63487UL,      64513UL,      65537UL,
    67589UL,      69653UL,      71693UL,      73727UL,
    75781UL,      77839UL,      79873UL,      81919UL,
    83969UL,      86017UL,      88069UL,      90121UL,
    92173UL,      94207UL,      96259UL,      98317UL,
    100357UL,     102407UL,     104459UL,     106501UL,
    108553UL,     110597UL,     112643UL,     114689UL,
    116741UL,     118787UL,     120833UL,     122887UL,
    124951UL,     126989UL,     129023UL,     131071UL,
    135173UL,     139267UL,     143387UL,     147457UL,
    151553UL,     155653UL,     159763UL,     163841UL,
    167953UL,     172031UL,     176129UL,     180233UL,
    184321UL,     188417UL,     192529UL,     196613UL,
    200713UL,     204803UL,     208907UL,     212999UL,
    217111UL,     221197UL,     225287UL,     229393UL,
    233477UL,     237571UL,     241663UL,     245759UL,
    249857UL,     253951UL,     258061UL,     262147UL,
    270337UL,     278543UL,     286721UL,     294911UL,
    303119UL,     311299UL,     319489UL,     327689UL,
    335879UL,     344083UL,     352267UL,     360457UL,
    368647UL,     376837UL,     385027UL,     393241UL,
    401407UL,     409609UL,     417793UL,     425987UL,
    434179UL,     442367UL,     450563UL,     458789UL,
    466951UL,     475141UL,     483337UL,     491527UL,
    499711UL,     507907UL,     516127UL,     524287UL,
    540677UL,     557057UL,     573451UL,     589829UL,
    606223UL,     622603UL,     638977UL,     655373UL,
    671743UL,     688133UL,     704521UL,     720899UL,
    737279UL,     753677UL,     770047UL,     786431UL,
    802829UL,     819229UL,     835591UL,     851971UL,
    868369UL,     884743UL,     901133UL,     917503UL,
    933893UL,     950281UL,     966659UL,     983063UL,
    999431UL,     1015813UL,    1032191UL,    1048583UL,
    1081351UL,    1114111UL,    1146881UL,    1179649UL,
    1212427UL,    1245187UL,    1277957UL,    1310719UL,
    1343491UL,    1376257UL,    1409027UL,    1441807UL,
    1474559UL,    1507369UL,    1540109UL,    1572869UL,
    1605631UL,    1638431UL,    1671191UL,    1703941UL,
    1736711UL,    1769473UL,    1802239UL,    1835017UL,
    1867783UL,    1900543UL,    1933331UL,    1966079UL,
    1998881UL,    2031671UL,    2064389UL,    2097169UL,
    2162717UL,    2228243UL,    2293771UL,    2359303UL,
    2424833UL,    2490377UL,    2555911UL,    2621447UL,
    2686979UL,    2752513UL,    2818103UL,    2883593UL,
    2949119UL,    3014659UL,    3080237UL,    3145739UL,
    3211279UL,    3276799UL,    3342341UL,    3407881UL,
    3473419UL,    3538949UL,    3604481UL,    3670027UL,
    3735553UL,    3801097UL,    3866623UL,    3932167UL,
    3997723UL,    4063237UL,    4128767UL,    4194319UL,
    4325389UL,    4456451UL,    4587533UL,    4718617UL,
    4849687UL,    4980749UL,    5111833UL,    5242883UL,
    5373971UL,    5505023UL,    5636123UL,    5767169UL,
    5898253UL,    6029329UL,    6160391UL,    6291469UL,
    6422531UL,    6553621UL,    6684673UL,    6815749UL,
    6946817UL,    7077893UL,    7208977UL,    7340033UL,
    7471127UL,    7602187UL,    7733251UL,    7864331UL,
    7995391UL,    8126473UL,    8257537UL,    8388617UL,
    8650753UL,    8912921UL,    9175057UL,    9437189UL,
    9699331UL,    9961487UL,    10223617UL,   10485767UL,
    10747903UL,   11010059UL,   11272193UL,   11534351UL,
    11796503UL,   12058679UL,   12320773UL,   12582917UL,
    12845069UL,   13107229UL,   13369399UL,   13631489UL,
    13893637UL,   14155777UL,   14417927UL,   14680063UL,
    14942209UL,   15204391UL,   15466499UL,   15728681UL,
    15990791UL,   16252967UL,   16515073UL,   16777259UL,
    17301517UL,   17825791UL,   18350093UL,   18874367UL,
    19398727UL,   19922993UL,   20447239UL,   20971529UL,
    21495809UL,   22020127UL,   22544387UL,   23068673UL,
    23592967UL,   24117257UL,   24641543UL,   25165843UL,
    25690121UL,   26214401UL,   26738687UL,   27262997UL,
    27787267UL,   28311553UL,   28835857UL,   29360147UL,
    29884417UL,   30408713UL,   30933011UL,   31457287UL,
    31981567UL,   32505901UL,   33030163UL,   33554467UL,
    34603013UL,   35651593UL,   36700159UL,   37748743UL,
    38797379UL,   39845887UL,   40894481UL,   41943049UL,
    42991621UL,   44040253UL,   45088781UL,   46137359UL,
    47185967UL,   48234517UL,   49283083UL,   50331653UL,
    51380233UL,   52428841UL,   53477453UL,   54525979UL,
    55574567UL,   56623153UL,   57671683UL,   58720267UL,
    59768831UL,   60817411UL,   61865989UL,   62914619UL,
    63963149UL,   65011717UL,   66060311UL,   67108879UL,
    69206017UL,   71303171UL,   73400329UL,   75497479UL,
    77594641UL,   79691779UL,   81788929UL,   83886091UL,
    85983239UL,   88080389UL,   90177541UL,   92274737UL,
    94371863UL,   96469001UL,   98566147UL,   100663319UL,
    102760453UL,  104857601UL,  106954759UL,  109051903UL,
    111149057UL,  113246209UL,  115343383UL,  117440551UL,
    119537689UL,  121634819UL,  123731977UL,  125829139UL,
    127926307UL,  130023431UL,  132120577UL,  134217757UL,
    138412031UL,  142606357UL,  146800649UL,  150994979UL,
    155189249UL,  159383563UL,  163577857UL,  167772161UL,
    171966481UL,  176160779UL,  180355079UL,  184549429UL,
    188743679UL,  192937991UL,  197132293UL,  201326611UL,
    205520911UL,  209715199UL,  213909503UL,  218103811UL,
    222298127UL,  226492433UL,  230686721UL,  234881033UL,
    239075327UL,  243269639UL,  247463939UL,  251658263UL,
    255852593UL,  260046847UL,  264241223UL,  268435459UL,
    276824071UL,  285212677UL,  293601283UL,  301989917UL,
    310378501UL,  318767107UL,  327155743UL,  335544323UL,
    343932959UL,  352321547UL,  360710167UL,  369098771UL,
    377487361UL,  385876021UL,  394264613UL,  402653189UL,
    411041831UL,  419430419UL,  427819031UL,  436207619UL,
    444596227UL,  452984849UL,  461373449UL,  469762049UL,
    478150661UL,  486539323UL,  494927929UL,  503316511UL,
    511705091UL,  520093703UL,  528482347UL,  536870923UL,
    553648171UL,  570425377UL,  587202571UL,  603979799UL,
    620757019UL,  637534277UL,  654311423UL,  671088667UL,
    687865859UL,  704643083UL,  721420307UL,  738197503UL,
    754974721UL,  771751961UL,  788529191UL,  805306457UL,
    822083597UL,  838860817UL,  855638023UL,  872415239UL,
    889192471UL,  905969671UL,  922746883UL,  939524129UL,
    956301317UL,  973078537UL,  989855747UL,  1006632983UL,
    1023410207UL, 1040187403UL, 1056964619UL, 1073741827UL,
    1107296257UL, 1140850699UL, 1174405129UL, 1207959559UL,
    1241514007UL, 1275068423UL, 1308622877UL, 1342177283UL,
    1375731737UL, 1409286161UL, 1442840579UL, 1476395029UL,
    1509949451UL, 1543503881UL, 1577058331UL, 1610612741UL,
    1644167233UL, 1677721631UL, 1711276033UL, 1744830469UL,
    1778384921UL, 1811939329UL, 1845493777UL, 1879048201UL,
    1912602623UL, 1946157079UL, 1979711497UL, 2013265921UL,
    2046820351UL, 2080374797UL, 2113929217UL, 2147483647UL,
    2214592529UL, 2281701377UL, 2348810279UL, 2415919109UL,
    2483027969UL, 2550136861UL, 2617245707UL, 2684354591UL,
    2751463433UL, 2818572287UL, 2885681153UL, 2952790033UL,
    3019898923UL, 3087007751UL, 3154116619UL, 3221225473UL,
    3288334369UL, 3355443229UL, 3422552069UL, 3489660929UL,
    3556769813UL, 3623878669UL, 3690987527UL, 3758096383UL,
    3825205247UL, 3892314113UL, 3959422979UL, 4026531853UL,
    4093640729UL, 4160749601UL, 4227858463UL
  };
  int shift=16;
  unsigned long cmp=0x1fffff;
  unsigned long x=num;
  unsigned int y=0;

  if(x<32)
  {
    static const unsigned long lowprimes[32]={
      1,1,2,3,
      5,5,7,7,
      11,11,11,11,
      13,13,17,17,
      17,17,19,19,
      23,23,23,23,
      29,29,29,29,
      29,29,31,31
    };
    return lowprimes[x];
  }

  while(shift)
  {
    if(x>cmp)
    {
      y+=shift;
      x>>=shift;
    }
    shift>>=1;
    cmp>>=shift;
  }

  /* For really large numbers, produce a number which is not
   * a prime, but hopefully good for hash tables, although I
   * seriously doubt anybody will use hashtables larger than
   * 1<<32 entries... /Hubbe
   */
  y = (y<<5)|(x&31);
  if(y >= NELEM(primes)) return num|7;

  return primes[y];
}
