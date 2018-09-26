#include "QuadFilterChain.h"
#include "SurgeStorage.h"
#include <vt_dsp/basic_dsp.h>
#include <vt_dsp/portable_intrinsics.h>

#if PPC
#define _mm_set1_ps(x) (vFloat)(x)

#define _mm_add_ps(x, y) vec_add(x, y)
#define _mm_sub_ps(x, y) vec_sub(x, y)
#define _mm_mul_ps vMul
#define _mm_and_ps(x, y) vec_and(x, y)
#define softclip_ps vec_softclip
#define _mm_load_ps(x) vec_ld(0, x)

#define _mm_add_ss(a, b) ((a) + (b))
#define sum_ps_to_ss(x) vec_hsum(x)
#define _mm_store_ss(adr, d) (*(adr) = (d))
#define _mm_load_ss(adr) (*(adr))
#endif

#define MWriteOutputs(x)                                                                           \
   d.OutL = _mm_add_ps(d.OutL, d.dOutL);                                                           \
   d.OutR = _mm_add_ps(d.OutR, d.dOutR);                                                           \
   __m128 outL = _mm_mul_ps(x, d.OutL);                                                            \
   __m128 outR = _mm_mul_ps(x, d.OutR);                                                            \
   _mm_store_ss(&OutL[k], _mm_add_ss(_mm_load_ss(&OutL[k]), sum_ps_to_ss(outL)));                  \
   _mm_store_ss(&OutR[k], _mm_add_ss(_mm_load_ss(&OutR[k]), sum_ps_to_ss(outR)));

#define MWriteOutputsDual(x, y)                                                                    \
   d.OutL = _mm_add_ps(d.OutL, d.dOutL);                                                           \
   d.OutR = _mm_add_ps(d.OutR, d.dOutR);                                                           \
   d.Out2L = _mm_add_ps(d.Out2L, d.dOut2L);                                                        \
   d.Out2R = _mm_add_ps(d.Out2R, d.dOut2R);                                                        \
   __m128 outL = vMAdd(x, d.OutL, vMul(y, d.Out2L));                                               \
   __m128 outR = vMAdd(x, d.OutR, vMul(y, d.Out2R));                                               \
   _mm_store_ss(&OutL[k], _mm_add_ss(_mm_load_ss(&OutL[k]), sum_ps_to_ss(outL)));                  \
   _mm_store_ss(&OutR[k], _mm_add_ss(_mm_load_ss(&OutR[k]), sum_ps_to_ss(outR)));

#if 0 // DEBUG
#define AssertReasonableAudioFloat(x) assert(x<32.f && x> - 32.f);
#else
#define AssertReasonableAudioFloat(x)
#endif

template <int config, bool A, bool WS, bool B>
void ProcessFBQuad(QuadFilterChainState& d, fbq_global& g, float* OutL, float* OutR)
{
   const __m128 hb_c = _mm_set1_ps(0.5f); // om denna ‰ndras frÂn 0.5, se till att ‰ndra i koden
                                          // eftersom det antas att den ‰r h‰lften
   const __m128 one = _mm_set1_ps(1.0f);

   switch (config)
   {
   case fb_serial: // no feedback at all  (saves CPU)
      // TODO denna skulle nog kunna bli ‰nnu snabbare om den serialiseras (vilket ‰r l‰ttare nu)
      // lite mer L1 v‰nlig, men latency-lalal blir nog detsamma
      for (int k = 0; k < block_size_os; k++)
      {
         __m128 input = d.DL[k];
         __m128 x = input, y = d.DR[k];

         if (A)
            x = g.FU1ptr(&d.FU[0], x);
         if (WS)
         {
            d.wsLPF = _mm_mul_ps(hb_c, _mm_add_ps(d.wsLPF, x));
            d.Drive = _mm_add_ps(d.Drive, d.dDrive);
            x = g.WSptr(d.wsLPF, d.Drive);
         }

         if (A || WS)
         {
            d.Mix1 = _mm_add_ps(d.Mix1, d.dMix1);
            x = _mm_add_ps(_mm_mul_ps(input, _mm_sub_ps(one, d.Mix1)), _mm_mul_ps(x, d.Mix1));
         }

         y = _mm_add_ps(x, y);

         if (B)
            y = g.FU2ptr(&d.FU[1], y);

         d.Mix2 = _mm_add_ps(d.Mix2, d.dMix2);
         x = _mm_add_ps(_mm_mul_ps(x, _mm_sub_ps(one, d.Mix2)), _mm_mul_ps(y, d.Mix2));
         d.Gain = _mm_add_ps(d.Gain, d.dGain);
         __m128 mask = _mm_load_ps((float*)&d.FU[0].active);
         __m128 out = _mm_and_ps(mask, _mm_mul_ps(x, d.Gain));

         // output stage
         MWriteOutputs(out)
      }
      break;
   case fb_serial2:
      for (int k = 0; k < block_size_os; k++)
      {
         d.FB = _mm_add_ps(d.FB, d.dFB);
         __m128 input = vMul(d.FB, d.FBlineL);
         input = vAdd(d.DL[k], softclip_ps(input));
         __m128 x = input, y = d.DR[k];

         if (A)
            x = g.FU1ptr(&d.FU[0], x);
         if (WS)
         {
            d.wsLPF = _mm_mul_ps(hb_c, _mm_add_ps(d.wsLPF, x));
            d.Drive = _mm_add_ps(d.Drive, d.dDrive);
            x = g.WSptr(d.wsLPF, d.Drive);
         }

         if (A || WS)
         {
            d.Mix1 = _mm_add_ps(d.Mix1, d.dMix1);
            x = _mm_add_ps(_mm_mul_ps(input, _mm_sub_ps(one, d.Mix1)), _mm_mul_ps(x, d.Mix1));
         }

         y = _mm_add_ps(x, y);

         if (B)
            y = g.FU2ptr(&d.FU[1], y);

         d.Mix2 = _mm_add_ps(d.Mix2, d.dMix2);
         x = _mm_add_ps(_mm_mul_ps(x, _mm_sub_ps(one, d.Mix2)), _mm_mul_ps(y, d.Mix2));
         d.Gain = _mm_add_ps(d.Gain, d.dGain);
         __m128 mask = _mm_load_ps((float*)&d.FU[0].active);
         __m128 out = _mm_and_ps(mask, _mm_mul_ps(x, d.Gain));
         d.FBlineL = out;

         // output stage
         MWriteOutputs(out)
      }
      break;
   case fb_serial3: // filter 2 is only heard in the feedback path, good for physical modelling with
                    // comb as f2
      for (int k = 0; k < block_size_os; k++)
      {
         d.FB = _mm_add_ps(d.FB, d.dFB);
         __m128 input = vMul(d.FB, d.FBlineL);
         input = vAdd(d.DL[k], softclip_ps(input));
         __m128 x = input, y = d.DR[k];

         if (A)
            x = g.FU1ptr(&d.FU[0], x);
         if (WS)
         {
            d.wsLPF = _mm_mul_ps(hb_c, _mm_add_ps(d.wsLPF, x));
            d.Drive = _mm_add_ps(d.Drive, d.dDrive);
            x = g.WSptr(d.wsLPF, d.Drive);
         }

         if (A || WS)
         {
            d.Mix1 = _mm_add_ps(d.Mix1, d.dMix1);
            x = _mm_add_ps(_mm_mul_ps(input, _mm_sub_ps(one, d.Mix1)), _mm_mul_ps(x, d.Mix1));
         }

         // output stage
         d.Gain = _mm_add_ps(d.Gain, d.dGain);
         __m128 mask = _mm_load_ps((float*)&d.FU[0].active);
         x = _mm_and_ps(mask, _mm_mul_ps(x, d.Gain));

         MWriteOutputs(x)

             y = _mm_add_ps(x, y);

         if (B)
            y = g.FU2ptr(&d.FU[1], y);

         d.Mix2 = _mm_add_ps(d.Mix2, d.dMix2);
         x = _mm_add_ps(_mm_mul_ps(x, _mm_sub_ps(one, d.Mix2)), _mm_mul_ps(y, d.Mix2));

         d.FBlineL = y;
      }
      break;
   case fb_dual:
      for (int k = 0; k < block_size_os; k++)
      {
         d.FB = _mm_add_ps(d.FB, d.dFB);
         __m128 fb = _mm_mul_ps(d.FB, d.FBlineL);
         fb = softclip_ps(fb);
         __m128 x = _mm_add_ps(d.DL[k], fb);
         __m128 y = _mm_add_ps(d.DR[k], fb);

         if (A)
            x = g.FU1ptr(&d.FU[0], x);
         if (B)
            y = g.FU2ptr(&d.FU[1], y);

         d.Mix1 = _mm_add_ps(d.Mix1, d.dMix1);
         d.Mix2 = _mm_add_ps(d.Mix2, d.dMix2);
         x = _mm_add_ps(_mm_mul_ps(x, d.Mix1), _mm_mul_ps(y, d.Mix2));

         if (WS)
         {
            d.wsLPF = _mm_mul_ps(hb_c, _mm_add_ps(d.wsLPF, x));
            d.Drive = _mm_add_ps(d.Drive, d.dDrive);
            x = g.WSptr(d.wsLPF, d.Drive);
         }

         d.Gain = _mm_add_ps(d.Gain, d.dGain);
         __m128 mask = _mm_load_ps((float*)&d.FU[0].active);
         __m128 out = _mm_and_ps(mask, _mm_mul_ps(x, d.Gain));
         d.FBlineL = out;
         // output stage
         MWriteOutputs(out)
      }
      break;
   case fb_dual2:
      for (int k = 0; k < block_size_os; k++)
      {
         d.FB = _mm_add_ps(d.FB, d.dFB);
         __m128 fb = _mm_mul_ps(d.FB, d.FBlineL);
         fb = softclip_ps(fb);
         __m128 x = _mm_add_ps(d.DL[k], fb);
         __m128 y = _mm_add_ps(d.DR[k], fb);

         if (A)
            x = g.FU1ptr(&d.FU[0], x);
         if (WS)
         {
            d.wsLPF = _mm_mul_ps(hb_c, _mm_add_ps(d.wsLPF, x));
            d.Drive = _mm_add_ps(d.Drive, d.dDrive);
            x = g.WSptr(d.wsLPF, d.Drive);
         }

         if (B)
            y = g.FU2ptr(&d.FU[1], y);

         d.Mix1 = _mm_add_ps(d.Mix1, d.dMix1);
         d.Mix2 = _mm_add_ps(d.Mix2, d.dMix2);
         x = _mm_add_ps(_mm_mul_ps(x, d.Mix1), _mm_mul_ps(y, d.Mix2));

         d.Gain = _mm_add_ps(d.Gain, d.dGain);
         __m128 mask = _mm_load_ps((float*)&d.FU[0].active);
         __m128 out = _mm_and_ps(mask, _mm_mul_ps(x, d.Gain));
         d.FBlineL = out;
         // output stage
         MWriteOutputs(out)
      }
      break;
   case fb_ring:
      for (int k = 0; k < block_size_os; k++)
      {
         d.FB = _mm_add_ps(d.FB, d.dFB);
         __m128 fb = _mm_mul_ps(d.FB, d.FBlineL);
         fb = softclip_ps(fb);
         __m128 x = _mm_add_ps(d.DL[k], fb);
         __m128 y = _mm_add_ps(d.DR[k], fb);

         if (A)
            x = g.FU1ptr(&d.FU[0], x);
         if (B)
            y = g.FU2ptr(&d.FU[1], y);

         d.Mix1 = _mm_add_ps(d.Mix1, d.dMix1);
         d.Mix2 = _mm_add_ps(d.Mix2, d.dMix2);

         x = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(_mm_sub_ps(one, d.Mix1), y), _mm_mul_ps(x, d.Mix1)),
                        _mm_add_ps(_mm_mul_ps(_mm_sub_ps(one, d.Mix2), x), _mm_mul_ps(y, d.Mix2)));

         if (WS)
         {
            d.wsLPF = _mm_mul_ps(hb_c, _mm_add_ps(d.wsLPF, x));
            d.Drive = _mm_add_ps(d.Drive, d.dDrive);
            x = g.WSptr(d.wsLPF, d.Drive);
         }

         d.Gain = _mm_add_ps(d.Gain, d.dGain);
         __m128 mask = _mm_load_ps((float*)&d.FU[0].active);
         __m128 out = _mm_and_ps(mask, _mm_mul_ps(x, d.Gain));
         d.FBlineL = out;
         // output stage
         MWriteOutputs(out)
      }
      break;
   case fb_stereo:
      for (int k = 0; k < block_size_os; k++)
      {
         d.FB = _mm_add_ps(d.FB, d.dFB);
         __m128 fb = _mm_mul_ps(d.FB, d.FBlineL);
         fb = softclip_ps(fb);
         __m128 x = _mm_add_ps(d.DL[k], fb);
         __m128 y = _mm_add_ps(d.DR[k], fb);

         if (A)
            x = g.FU1ptr(&d.FU[0], x);
         if (B)
            y = g.FU2ptr(&d.FU[1], y);

         if (WS)
         {
            d.Drive = _mm_add_ps(d.Drive, d.dDrive);
            x = g.WSptr(x, d.Drive);
            y = g.WSptr(y, d.Drive);
         }

         d.Mix1 = _mm_add_ps(d.Mix1, d.dMix1);
         d.Mix2 = _mm_add_ps(d.Mix2, d.dMix2);
         x = _mm_mul_ps(x, d.Mix1);
         y = _mm_mul_ps(y, d.Mix2);

         d.Gain = _mm_add_ps(d.Gain, d.dGain);
         __m128 mask = _mm_load_ps((float*)&d.FU[0].active);
         x = _mm_and_ps(mask, _mm_mul_ps(x, d.Gain));
         y = _mm_and_ps(mask, _mm_mul_ps(y, d.Gain));
         d.FBlineL = _mm_add_ps(x, y);

         // output stage
         MWriteOutputsDual(x, y) AssertReasonableAudioFloat(OutL[k]);
         AssertReasonableAudioFloat(OutR[k]);
      }
      break;
   case fb_wide:
      for (int k = 0; k < block_size_os; k++)
      {
         d.FB = _mm_add_ps(d.FB, d.dFB);
         __m128 fbL = _mm_mul_ps(d.FB, d.FBlineL);
         __m128 fbR = _mm_mul_ps(d.FB, d.FBlineR);
         __m128 xin = _mm_add_ps(d.DL[k], softclip_ps(fbL));
         __m128 yin = _mm_add_ps(d.DR[k], softclip_ps(fbR));
         __m128 x = xin;
         __m128 y = yin;

         if (A)
         {
            x = g.FU1ptr(&d.FU[0], x);
            y = g.FU1ptr(&d.FU[2], y);
         }

         if (WS)
         {
            d.Drive = _mm_add_ps(d.Drive, d.dDrive);
            x = g.WSptr(x, d.Drive);
            y = g.WSptr(y, d.Drive);
         }

         if (A || WS)
         {
            d.Mix1 = _mm_add_ps(d.Mix1, d.dMix1);
            __m128 t = _mm_sub_ps(one, d.Mix1);
            x = _mm_add_ps(_mm_mul_ps(xin, t), _mm_mul_ps(x, d.Mix1));
            y = _mm_add_ps(_mm_mul_ps(yin, t), _mm_mul_ps(y, d.Mix1));
         }

         if (B)
         {
            __m128 z = g.FU2ptr(&d.FU[1], x);
            __m128 w = g.FU2ptr(&d.FU[3], y);

            d.Mix2 = _mm_add_ps(d.Mix2, d.dMix2);
            __m128 t = _mm_sub_ps(one, d.Mix2);
            x = _mm_add_ps(_mm_mul_ps(x, t), _mm_mul_ps(z, d.Mix2));
            y = _mm_add_ps(_mm_mul_ps(y, t), _mm_mul_ps(w, d.Mix2));
         }

         d.Gain = _mm_add_ps(d.Gain, d.dGain);
         __m128 mask = _mm_load_ps((float*)&d.FU[0].active);
         x = _mm_and_ps(mask, _mm_mul_ps(x, d.Gain));
         y = _mm_and_ps(mask, _mm_mul_ps(y, d.Gain));
         d.FBlineL = x;
         d.FBlineR = y;

         // output stage
         MWriteOutputsDual(x, y) AssertReasonableAudioFloat(OutL[k]);
         AssertReasonableAudioFloat(OutR[k]);
      }
      break;
   }
}

template <int config> FBQFPtr GetFBQPointer2(bool A, bool WS, bool B)
{
   if (A)
   {
      if (B)
      {
         if (WS)
            return ProcessFBQuad<config, 1, 1, 1>;
         else
            return ProcessFBQuad<config, 1, 0, 1>;
      }
      else
      {
         if (WS)
            return ProcessFBQuad<config, 1, 1, 0>;
         else
            return ProcessFBQuad<config, 1, 0, 0>;
      }
   }
   else
   {
      if (B)
      {
         if (WS)
            return ProcessFBQuad<config, 0, 1, 1>;
         else
            return ProcessFBQuad<config, 0, 0, 1>;
      }
      else
      {
         if (WS)
            return ProcessFBQuad<config, 0, 1, 0>;
         else
            return ProcessFBQuad<config, 0, 0, 0>;
      }
   }
   return 0;
}

FBQFPtr GetFBQPointer(int config, bool A, bool WS, bool B)
{
   switch (config)
   {
   case fb_serial:
      return GetFBQPointer2<fb_serial>(A, WS, B);
   case fb_serial2:
      return GetFBQPointer2<fb_serial2>(A, WS, B);
   case fb_serial3:
      return GetFBQPointer2<fb_serial3>(A, WS, B);
   case fb_dual:
      return GetFBQPointer2<fb_dual>(A, WS, B);
   case fb_dual2:
      return GetFBQPointer2<fb_dual2>(A, WS, B);
   case fb_ring:
      return GetFBQPointer2<fb_ring>(A, WS, B);
   case fb_stereo:
      return GetFBQPointer2<fb_stereo>(A, WS, B);
   case fb_wide:
      return GetFBQPointer2<fb_wide>(A, WS, B);
   }
   return 0;
}
