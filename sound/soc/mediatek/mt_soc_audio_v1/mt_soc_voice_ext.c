/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*******************************************************************************
 *
 * Filename:
 * ---------
 *   mt_soc_voice_ext.c
 *
 * Project:
 * --------
 *   voice call platform driver
 *
 * Description:
 * ------------
 *
 *
 * Author:
 * -------
 * Chipeng Chang
 *
 *------------------------------------------------------------------------------
 * $Revision: #1 $
 * $Modtime:$
 * $Log:$
 *
 *
 *******************************************************************************/


/*****************************************************************************
 *                     C O M P I L E R   F L A G S
 *****************************************************************************/


/*****************************************************************************
 *                E X T E R N A L   R E F E R E N C E S
 *****************************************************************************/

#include "AudDrv_Common.h"
#include "AudDrv_Def.h"
#include "AudDrv_Afe.h"
#include "AudDrv_Ana.h"
#include "AudDrv_Clk.h"
#include "AudDrv_Kernel.h"
#include "mt_soc_afe_control.h"
#include "mt_soc_digital_type.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/completion.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/wakelock.h>
#include <linux/semaphore.h>
#include <linux/jiffies.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/xlog.h>
#include <mach/irqs.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <mach/mt_reg_base.h>
#include <asm/div64.h>
#include <linux/aee.h>
#include <mach/pmic_mt6320_sw.h>
#include <mach/upmu_common.h>
#include <mach/upmu_hw.h>

#include <mach/mt_gpio.h>
#include <mach/mt_typedefs.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/jack.h>
#include <asm/mach-types.h>
#include <sound/mt_soc_audio.h>

/*
 *    function implementation
 */

static int mtk_voice_ext_probe(struct platform_device *pdev);
static int mtk_voice_ext_close(struct snd_pcm_substream *substream);
static int mtk_soc_voice_ext_new(struct snd_soc_pcm_runtime *rtd);
static int mtk_voice_ext_platform_probe(struct snd_soc_platform *platform);

#define MAX_PCM_DEVICES     4
#define MAX_PCM_SUBSTREAMS  128
#define MAX_MIDI_DEVICES 0

/* defaults */
#define MAX_BUFFER_SIZE     (16*1024)
#define MIN_PERIOD_SIZE     64
#define MAX_PERIOD_SIZE     MAX_BUFFER_SIZE
#define USE_FORMATS         (SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE)
#define USE_RATE        SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000
#define USE_RATE_MIN        8000
#define USE_RATE_MAX        48000
#define USE_CHANNELS_MIN    1
#define USE_CHANNELS_MAX    2
#define USE_PERIODS_MIN      512
#define USE_PERIODS_MAX     2048

static bool Voice_Ext_Status = false;
static AudioDigtalI2S mAudioDigitalI2S;

bool get_voice_ext_status(void)
{
    return Voice_Ext_Status;
}
EXPORT_SYMBOL(get_voice_ext_status);
static AudioDigitalPCM  VoiceExtPcm =
{
    .mBclkInInv = true,
    .mTxLchRepeatSel = Soc_Aud_TX_LCH_RPT_TX_LCH_NO_REPEAT,
    .mVbt16kModeSel  = Soc_Aud_VBT_16K_MODE_VBT_16K_MODE_ENABLE,
    .mExtModemSel = Soc_Aud_EXT_MODEM_MODEM_2_USE_EXTERNAL_MODEM,
    .mExtendBckSyncLength = 0,
    .mExtendBckSyncTypeSel = Soc_Aud_PCM_SYNC_TYPE_BCK_CYCLE_SYNC,
    .mSingelMicSel = Soc_Aud_BT_MODE_DUAL_MIC_ON_TX,
    .mAsyncFifoSel = Soc_Aud_BYPASS_SRC_SLAVE_USE_ASRC,
    .mSlaveModeSel = Soc_Aud_PCM_CLOCK_SOURCE_SALVE_MODE,
    .mPcmWordLength = Soc_Aud_PCM_WLEN_LEN_PCM_16BIT,
    .mPcmModeWidebandSel = false,
    .mPcmFormat = Soc_Aud_PCM_FMT_PCM_MODE_B,
    .mModemPcmOn = false,
};

/* Conventional and unconventional sample rate supported */
static unsigned int supported_sample_rates[] =
{
    8000,  16000, 32000,
};

static struct snd_pcm_hw_constraint_list constraints_sample_rates =
{
    .count = ARRAY_SIZE(supported_sample_rates),
    .list = supported_sample_rates,
    .mask = 0,
};

static struct snd_pcm_hardware mtk_pcm_hardware =
{
    .info = (SNDRV_PCM_INFO_MMAP |
    SNDRV_PCM_INFO_INTERLEAVED |
    SNDRV_PCM_INFO_RESUME |
    SNDRV_PCM_INFO_MMAP_VALID),
    .formats =      USE_FORMATS,
    .rates =        USE_RATE,
    .rate_min =     USE_RATE_MIN,
    .rate_max =     USE_RATE_MAX,
    .channels_min =     USE_CHANNELS_MIN,
    .channels_max =     USE_CHANNELS_MAX,
    .buffer_bytes_max = MAX_BUFFER_SIZE,
    .period_bytes_max = MAX_PERIOD_SIZE,
    .periods_min =      1,
    .periods_max =      4096,
    .fifo_size =        0,
};

static int mtk_voice_ext_pcm_open(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    int err = 0;
    int ret = 0;
    AudDrv_Clk_On();
    AudDrv_ADC_Clk_On();

    printk("mtk_voice_ext_pcm_open\n");

    if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
    {
        printk("%s  with SNDRV_PCM_STREAM_CAPTURE \n",__func__);
        runtime->rate = 16000;
        return 0;
    }
    runtime->hw = mtk_pcm_hardware;
    memcpy((void *)(&(runtime->hw)), (void *)&mtk_pcm_hardware , sizeof(struct snd_pcm_hardware));

    ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
                                     &constraints_sample_rates);
    ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

    if (ret < 0)
    {
        printk("snd_pcm_hw_constraint_integer failed\n");
    }

    //print for hw pcm information
    printk("mtk_voice_ext_pcm_open runtime rate = %d channels = %d \n", runtime->rate, runtime->channels);

    runtime->hw.info |= SNDRV_PCM_INFO_INTERLEAVED;
    runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;

    if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
    {
        printk("SNDRV_PCM_STREAM_PLAYBACK mtkalsa_voice_ext_constraints\n");
        runtime->rate = 16000;
    }
    else
    {

    }

    if (err < 0)
    {
        printk("mtk_voice_ext_close\n");
        mtk_voice_ext_close(substream);
        return err;
    }
    printk("mtk_voice_ext_pcm_open return\n");
    return 0;
}

static void ConfigAdcI2S(struct snd_pcm_substream *substream)
{
    mAudioDigitalI2S.mLR_SWAP = Soc_Aud_LR_SWAP_NO_SWAP;
    mAudioDigitalI2S.mBuffer_Update_word = 8;
    mAudioDigitalI2S.mFpga_bit_test = 0;
    mAudioDigitalI2S.mFpga_bit = 0;
    mAudioDigitalI2S.mloopback = 0;
    mAudioDigitalI2S.mINV_LRCK = Soc_Aud_INV_LRCK_NO_INVERSE;
    mAudioDigitalI2S.mI2S_FMT = Soc_Aud_I2S_FORMAT_I2S;
    mAudioDigitalI2S.mI2S_WLEN = Soc_Aud_I2S_WLEN_WLEN_16BITS;
    mAudioDigitalI2S.mI2S_SAMPLERATE = (substream->runtime->rate);
}

static int mtk_voice_ext_close(struct snd_pcm_substream *substream)
{
    printk("mtk_voice_ext_close \n");

    if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
    {
        printk("%s  with SNDRV_PCM_STREAM_CAPTURE \n",__func__);
        return 0;
    }

    //  todo : enable sidetone
    // here start digital part
    SetConnection(Soc_Aud_InterCon_DisConnect, Soc_Aud_InterConnectionInput_I03, Soc_Aud_InterConnectionOutput_O07);
    SetConnection(Soc_Aud_InterCon_DisConnect, Soc_Aud_InterConnectionInput_I04, Soc_Aud_InterConnectionOutput_O08);
    SetConnection(Soc_Aud_InterCon_DisConnect, Soc_Aud_InterConnectionInput_I09, Soc_Aud_InterConnectionOutput_O03);
    SetConnection(Soc_Aud_InterCon_DisConnect, Soc_Aud_InterConnectionInput_I09, Soc_Aud_InterConnectionOutput_O04);

    SetI2SAdcEnable(false);
    SetI2SDacEnable(false);
    SetModemPcmEnable(MODEM_EXTERNAL, false);
    SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_DAC, false);
    SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_IN_ADC, false);

    EnableAfe(false);

    Voice_Ext_Status = false;

    return 0;
}

static int mtk_voice_ext_trigger(struct snd_pcm_substream *substream, int cmd)
{
    printk("mtk_voice_ext_trigger cmd = %d\n", cmd);
    switch (cmd)
    {
        case SNDRV_PCM_TRIGGER_START:
        case SNDRV_PCM_TRIGGER_RESUME:
        case SNDRV_PCM_TRIGGER_STOP:
        case SNDRV_PCM_TRIGGER_SUSPEND:
            break;
    }
    return 0;
}

static int mtk_voice_ext_pcm_copy(struct snd_pcm_substream *substream,
                              int channel, snd_pcm_uframes_t pos,
                              void __user *dst, snd_pcm_uframes_t count)
{
    return 0;
}

static int mtk_voice_ext_pcm_silence(struct snd_pcm_substream *substream,
                                 int channel, snd_pcm_uframes_t pos,
                                 snd_pcm_uframes_t count)
{
    printk("mtk_voice_ext_pcm_silence \n");
    return 0; /* do nothing */
}

static void *dummy_page[2];
static struct page *mtk_pcm_page(struct snd_pcm_substream *substream,
                                 unsigned long offset)
{
    return virt_to_page(dummy_page[substream->stream]); /* the same page */
}

static int mtk_voice1_ext_prepare(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtimeStream = substream->runtime;
    printk("mtk_alsa_prepare rate = %d  channels = %d period_size = %lu\n",
           runtimeStream->rate, runtimeStream->channels, runtimeStream->period_size);

    if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
    {
        printk("%s  with SNDRV_PCM_STREAM_CAPTURE \n",__func__);
        return 0;
    }
    // here start digital part
    SetConnection(Soc_Aud_InterCon_Connection, Soc_Aud_InterConnectionInput_I03, Soc_Aud_InterConnectionOutput_O07);
    SetConnection(Soc_Aud_InterCon_Connection, Soc_Aud_InterConnectionInput_I04, Soc_Aud_InterConnectionOutput_O08);
    SetConnection(Soc_Aud_InterCon_Connection, Soc_Aud_InterConnectionInput_I09, Soc_Aud_InterConnectionOutput_O03);
    SetConnection(Soc_Aud_InterCon_Connection, Soc_Aud_InterConnectionInput_I09, Soc_Aud_InterConnectionOutput_O04);

    // start I2S DAC out
    SetI2SDacOut(substream->runtime->rate);
    SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_DAC, true);

    ConfigAdcI2S(substream);
    SetI2SAdcIn(&mAudioDigitalI2S);
    SetI2SDacEnable(true);

    SetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_IN_ADC, true);
    SetI2SAdcEnable(true);
    EnableAfe(true);
    VoiceExtPcm.mPcmModeWidebandSel = (runtimeStream->rate == 8000) ? Soc_Aud_PCM_MODE_PCM_MODE_8K : Soc_Aud_PCM_MODE_PCM_MODE_16K;
    //VoiceExtPcm.mAsyncFifoSel = Soc_Aud_BYPASS_SRC_SLAVE_USE_ASYNC_FIFO;
    SetModemPcmConfig(MODEM_EXTERNAL, VoiceExtPcm);
    SetModemPcmEnable(MODEM_EXTERNAL, true);

    Voice_Ext_Status = true;

    return 0;
}

static int mtk_pcm_hw_params(struct snd_pcm_substream *substream,
                             struct snd_pcm_hw_params *hw_params)
{
    int ret = 0;
    printk("mtk_pcm_hw_params \n");
    return ret;
}

static int mtk_voice_ext_hw_free(struct snd_pcm_substream *substream)
{
    PRINTK_AUDDRV("mtk_voice_ext_hw_free \n");
    return snd_pcm_lib_free_pages(substream);
}

static struct snd_pcm_ops mtk_voice_ext_ops =
{
    .open =     mtk_voice_ext_pcm_open,
    .close =    mtk_voice_ext_close,
    .ioctl =    snd_pcm_lib_ioctl,
    .hw_params =    mtk_pcm_hw_params,
    .hw_free =  mtk_voice_ext_hw_free,
    .prepare =  mtk_voice1_ext_prepare,
    .trigger =  mtk_voice_ext_trigger,
    .copy =     mtk_voice_ext_pcm_copy,
    .silence =  mtk_voice_ext_pcm_silence,
    .page =     mtk_pcm_page,
};

static struct snd_soc_platform_driver mtk_soc_voice_ext_platform =
{
    .ops        = &mtk_voice_ext_ops,
    .pcm_new    = mtk_soc_voice_ext_new,
    .probe      = mtk_voice_ext_platform_probe,
};

static int mtk_voice_ext_probe(struct platform_device *pdev)
{
    printk("mtk_voice_ext_probe\n");
    if (pdev->dev.of_node)
    {
        dev_set_name(&pdev->dev, "%s", MT_SOC_VOICE_EXT);
    }
    printk("%s: dev name %s\n", __func__, dev_name(&pdev->dev));
    return snd_soc_register_platform(&pdev->dev,
                                     &mtk_soc_voice_ext_platform);
}

static int mtk_soc_voice_ext_new(struct snd_soc_pcm_runtime *rtd)
{
    int ret = 0;
    printk("%s\n", __func__);
    return ret;
}

static int mtk_voice_ext_platform_probe(struct snd_soc_platform *platform)
{
    printk("mtk_voice_ext_platform_probe\n");
    return 0;
}

static int mtk_voice_ext_remove(struct platform_device *pdev)
{
    pr_debug("%s\n", __func__);
    snd_soc_unregister_platform(&pdev->dev);
    return 0;
}


//supend and resume function
static int mtk_voice_ext_pm_ops_suspend(struct device *device)
{
   // if now in phone call state, not suspend!!
    bool b_modem1_speech_on;
    bool b_modem2_speech_on;
    AudDrv_Clk_On();//should enable clk for access reg
    b_modem1_speech_on = (bool)(Afe_Get_Reg(PCM2_INTF_CON) & 0x1);
    b_modem2_speech_on = (bool)(Afe_Get_Reg(PCM_INTF_CON) & 0x1);
    AudDrv_Clk_Off();//should enable clk for access reg
    if (b_modem1_speech_on == true || b_modem2_speech_on == true)
    {
        clkmux_sel(MT_MUX_AUDINTBUS, 0, "AUDIO"); //select 26M
        return 0;
    }
    return 0;
}

static int mtk_voice_ext_pm_ops_resume(struct device *device)
{
    bool b_modem1_speech_on;
    bool b_modem2_speech_on;
    AudDrv_Clk_On();//should enable clk for access reg
    b_modem1_speech_on = (bool)(Afe_Get_Reg(PCM2_INTF_CON) & 0x1);
    b_modem2_speech_on = (bool)(Afe_Get_Reg(PCM_INTF_CON) & 0x1);
    AudDrv_Clk_Off();
    if (b_modem1_speech_on == true || b_modem2_speech_on == true)
    {
        clkmux_sel(MT_MUX_AUDINTBUS, 1, "AUDIO"); //mainpll
        return 0;
    }
    return 0;
}

struct dev_pm_ops mtk_voice_ext_pm_ops =
{
    .suspend = mtk_voice_ext_pm_ops_suspend,
    .resume = mtk_voice_ext_pm_ops_resume,
    .freeze = NULL,
    .thaw = NULL,
    .poweroff = NULL,
    .restore = NULL,
    .restore_noirq = NULL,
};


static struct platform_driver mtk_voice_ext_driver =
{
    .driver = {
        .name = MT_SOC_VOICE_EXT,
        .owner = THIS_MODULE,
#ifdef CONFIG_PM
        .pm     = &mtk_voice_ext_pm_ops,
#endif
    },
    .probe = mtk_voice_ext_probe,
    .remove = mtk_voice_ext_remove,
};

static struct platform_device *soc_mtk_voice_ext_dev;

static int __init mtk_soc_voice_ext_platform_init(void)
{
    int ret = 0;
    printk("%s\n", __func__);
    soc_mtk_voice_ext_dev = platform_device_alloc(MT_SOC_VOICE_EXT , -1);
    if (!soc_mtk_voice_ext_dev)
    {
        return -ENOMEM;
    }

    ret = platform_device_add(soc_mtk_voice_ext_dev);
    if (ret != 0)
    {
        platform_device_put(soc_mtk_voice_ext_dev);
        return ret;
    }

    ret = platform_driver_register(&mtk_voice_ext_driver);

    return ret;

}
module_init(mtk_soc_voice_ext_platform_init);

static void __exit mtk_soc_voice_ext_platform_exit(void)
{

    printk("%s\n", __func__);
    platform_driver_unregister(&mtk_voice_ext_driver);
}
module_exit(mtk_soc_voice_ext_platform_exit);

MODULE_DESCRIPTION("AFE PCM module platform driver");
MODULE_LICENSE("GPL");


