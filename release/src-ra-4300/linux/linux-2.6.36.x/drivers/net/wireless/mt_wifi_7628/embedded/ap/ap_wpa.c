/*
 ***************************************************************************
 * Ralink Tech Inc.
 * 4F, No. 2 Technology 5th Rd.
 * Science-based Industrial Park
 * Hsin-chu, Taiwan, R.O.C.
 *
 * (c) Copyright 2002, Ralink Technology, Inc.
 *
 * All rights reserved. Ralink's source code is an unpublished work and the
 * use of a copyright notice does not imply otherwise. This source code
 * contains confidential trade secret material of Ralink Tech. Any attemp
 * or participation in deciphering, decoding, reverse engineering or in any
 * way altering the source code is stricitly prohibited, unless the prior
 * written consent of Ralink Technology, Inc. is obtained.
 ***************************************************************************

    Module Name:
    wpa.c

    Abstract:

    Revision History:
    Who         When            What
    --------    ----------      ----------------------------------------------
    Jan Lee     03-07-22        Initial
    Rory Chen   04-11-29        Add WPA2PSK
*/
#include "rt_config.h"

extern UCHAR	EAPOL[];

/*
    ==========================================================================
    Description:
        Port Access Control Inquiry function. Return entry's Privacy and Wpastate.
    Return:
        pEntry 
    ==========================================================================
*/
MAC_TABLE_ENTRY *PACInquiry(RTMP_ADAPTER *pAd, UCHAR Wcid)
{
    MAC_TABLE_ENTRY *pEntry = NULL;

	if (Wcid < MAX_LEN_OF_MAC_TABLE)
		pEntry = &(pAd->MacTab.Content[Wcid]);

    return pEntry;
}

/*
    ==========================================================================
    Description:
       Check sanity of multicast cipher selector in RSN IE.
    Return:
         TRUE if match
         FALSE otherwise
    ==========================================================================
*/
BOOLEAN RTMPCheckMcast(
    IN PRTMP_ADAPTER    pAd,
    IN PEID_STRUCT      eid_ptr,
    IN MAC_TABLE_ENTRY  *pEntry)
{
	UCHAR apidx;
	struct wifi_dev *wdev;


	ASSERT(pEntry);
	ASSERT(pEntry->func_tb_idx < pAd->ApCfg.BssidNum);

	apidx = pEntry->func_tb_idx;

	wdev = &pAd->ApCfg.MBSSID[apidx].wdev;
	pEntry->AuthMode = wdev->AuthMode;

	if (eid_ptr->Len >= 6)
	{
        /* WPA and WPA2 format not the same in RSN_IE */
        if (eid_ptr->Eid == IE_WPA)
        {
            if (wdev->AuthMode == Ndis802_11AuthModeWPA1WPA2)
                pEntry->AuthMode = Ndis802_11AuthModeWPA;
            else if (wdev->AuthMode == Ndis802_11AuthModeWPA1PSKWPA2PSK)
                pEntry->AuthMode = Ndis802_11AuthModeWPAPSK;

            if (NdisEqualMemory(&eid_ptr->Octet[6], &pAd->ApCfg.MBSSID[apidx].RSN_IE[0][6], 4))
                return TRUE;
        }
        else if (eid_ptr->Eid == IE_WPA2)
        {
            UCHAR   IE_Idx = 0;

            /* When WPA1/WPA2 mix mode, the RSN_IE is stored in different structure */
            if ((wdev->AuthMode == Ndis802_11AuthModeWPA1WPA2) || 
                (wdev->AuthMode == Ndis802_11AuthModeWPA1PSKWPA2PSK))
                IE_Idx = 1;
    
            if (wdev->AuthMode == Ndis802_11AuthModeWPA1WPA2)
                pEntry->AuthMode = Ndis802_11AuthModeWPA2;
            else if (wdev->AuthMode == Ndis802_11AuthModeWPA1PSKWPA2PSK)
                pEntry->AuthMode = Ndis802_11AuthModeWPA2PSK;

            if (NdisEqualMemory(&eid_ptr->Octet[2], &pAd->ApCfg.MBSSID[apidx].RSN_IE[IE_Idx][2], 4))
                return TRUE;
        }
    }

    return FALSE;
}

/*
    ==========================================================================
    Description:
       Check sanity of unicast cipher selector in RSN IE.
    Return:
         TRUE if match
         FALSE otherwise
    ==========================================================================
*/
BOOLEAN RTMPCheckUcast(
    IN PRTMP_ADAPTER    pAd,
    IN PEID_STRUCT      eid_ptr,
    IN MAC_TABLE_ENTRY	*pEntry)
{
	PUCHAR 	pStaTmp;
	USHORT	Count;
	UCHAR 	apidx;
	struct wifi_dev *wdev;

	ASSERT(pEntry);
	ASSERT(pEntry->func_tb_idx < pAd->ApCfg.BssidNum);

	apidx = pEntry->func_tb_idx;
	wdev = &pAd->ApCfg.MBSSID[apidx].wdev;

	pEntry->WepStatus = wdev->WepStatus;
    
#ifndef CONFIG_SECURITY_IMPROVEMENT_SUPPORT
	if (eid_ptr->Len < 16) {
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("[ERROR]RTMPCheckUcast : the length is too short(%d) \n", eid_ptr->Len));
		return FALSE;
	}
#endif
	/* Store STA RSN_IE capability */
	pStaTmp = (PUCHAR)&eid_ptr->Octet[0];
	if(eid_ptr->Eid == IE_WPA2)
	{
		/* skip Version(2),Multicast cipter(4) 2+4==6 */
		/* point to number of unicast */
        pStaTmp +=6;
	}
	else if (eid_ptr->Eid == IE_WPA)	
	{
		/* skip OUI(4),Vesrion(2),Multicast cipher(4) 4+2+4==10 */
		/* point to number of unicast */
		pStaTmp += 10;
	}
	else
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("[ERROR]RTMPCheckUcast : invalid IE=%d\n", eid_ptr->Eid));
	    return FALSE;
	}

	/* Store unicast cipher count */
    NdisMoveMemory(&Count, pStaTmp, sizeof(USHORT));
    Count = cpu2le16(Count);		


	/* pointer to unicast cipher */
    pStaTmp += sizeof(USHORT);	
#ifdef CONFIG_SECURITY_IMPROVEMENT_SUPPORT
	if (eid_ptr->Len >= 12)
#else
	if (eid_ptr->Len >= 16)
#endif
    {
    	if (eid_ptr->Eid == IE_WPA)
    	{
    		if (wdev->WepStatus == Ndis802_11TKIPAESMix)
			{/* multiple cipher (TKIP/CCMP) */

				while (Count > 0)
				{
					/* TKIP */
					if (MIX_CIPHER_WPA_TKIP_ON(wdev->WpaMixPairCipher))
					{
						/* Compare if peer STA uses the TKIP as its unicast cipher */
					if (RTMPEqualMemory(pStaTmp, &pAd->ApCfg.MBSSID[apidx].RSN_IE[0][12], 4))
					{
						pEntry->WepStatus = Ndis802_11TKIPEnable;
						return TRUE;
					}

						/* Our AP uses the AES as the secondary cipher */
						/* Compare if the peer STA use AES as its unicast cipher */
						if (MIX_CIPHER_WPA_AES_ON(wdev->WpaMixPairCipher))
						{
							if (RTMPEqualMemory(pStaTmp, &pAd->ApCfg.MBSSID[apidx].RSN_IE[0][16], 4))
							{
								pEntry->WepStatus = Ndis802_11AESEnable;
								return TRUE;
							}
						}
					}
					else
					{
					/* AES */
						if (RTMPEqualMemory(pStaTmp, &pAd->ApCfg.MBSSID[apidx].RSN_IE[0][12], 4))
					{
						pEntry->WepStatus = Ndis802_11AESEnable;
						return TRUE;
					}
					}
															
					pStaTmp += 4;
					Count--;
				}
    		}
    		else
    		{/* single cipher */
    			while (Count > 0)
    			{
    				if (RTMPEqualMemory(pStaTmp , &pAd->ApCfg.MBSSID[apidx].RSN_IE[0][12], 4))
		            	return TRUE;

					pStaTmp += 4;
					Count--;
				}
    		}
    	}
    	else if (eid_ptr->Eid == IE_WPA2)
    	{
    		UCHAR	IE_Idx = 0;

			/* When WPA1/WPA2 mix mode, the RSN_IE is stored in different structure */
			if ((wdev->AuthMode == Ndis802_11AuthModeWPA1WPA2) || (wdev->AuthMode == Ndis802_11AuthModeWPA1PSKWPA2PSK))
				IE_Idx = 1;
	
			if (wdev->WepStatus == Ndis802_11TKIPAESMix)
			{/* multiple cipher (TKIP/CCMP) */

				while (Count > 0)
    			{
    				/* WPA2 TKIP */
					if (MIX_CIPHER_WPA2_TKIP_ON(wdev->WpaMixPairCipher))
					{
						/* Compare if peer STA uses the TKIP as its unicast cipher */
					if (RTMPEqualMemory(pStaTmp, &pAd->ApCfg.MBSSID[apidx].RSN_IE[IE_Idx][8], 4))
					{
						pEntry->WepStatus = Ndis802_11TKIPEnable;
						return TRUE;
					}

						/* Our AP uses the AES as the secondary cipher */
						/* Compare if the peer STA use AES as its unicast cipher */
						if (MIX_CIPHER_WPA2_AES_ON(wdev->WpaMixPairCipher))
						{
							if (RTMPEqualMemory(pStaTmp, &pAd->ApCfg.MBSSID[apidx].RSN_IE[IE_Idx][12], 4))
							{
								pEntry->WepStatus = Ndis802_11AESEnable;
								return TRUE;
							}
						}
					}
					else
					{
					/* AES */
						if (RTMPEqualMemory(pStaTmp, &pAd->ApCfg.MBSSID[apidx].RSN_IE[IE_Idx][8], 4))
					{
						pEntry->WepStatus = Ndis802_11AESEnable;
						return TRUE;
					}
					}
				    				
					pStaTmp += 4;
					Count--;
				}
			}
			else
			{/* single cipher */
				while (Count > 0)
    			{
					if (RTMPEqualMemory(pStaTmp, &pAd->ApCfg.MBSSID[apidx].RSN_IE[IE_Idx][8], 4))
						return TRUE;

					pStaTmp += 4;
					Count--;
				}
			}
    	}
    }

    return FALSE;
}


/*
    ==========================================================================
    Description:
       Check invalidity of authentication method selection in RSN IE.
    Return:
         TRUE if match
         FALSE otherwise
    ==========================================================================
*/
BOOLEAN RTMPCheckAKM(PUCHAR sta_akm, PUCHAR ap_rsn_ie, INT iswpa2
#if defined(DOT11_SAE_SUPPORT) || defined(CONFIG_OWE_SUPPORT)
			, NDIS_802_11_AUTHENTICATION_MODE APAuthMode, MAC_TABLE_ENTRY * pEntry
#endif
			)
{
	PUCHAR pTmp;
	USHORT Count;

	pTmp = ap_rsn_ie;

	if(iswpa2)
    /* skip Version(2),Multicast cipter(4) 2+4==6 */
        pTmp +=6;
    else	
    /*skip OUI(4),Vesrion(2),Multicast cipher(4) 4+2+4==10 */
        pTmp += 10;/*point to number of unicast */
	    
    NdisMoveMemory(&Count, pTmp, sizeof(USHORT));	
    Count = cpu2le16(Count);		

    pTmp   += sizeof(USHORT);/*pointer to unicast cipher */

    /* Skip all unicast cipher suite */
    while (Count > 0)
    	{
		/* Skip OUI */
		pTmp += 4;
		Count--;
	}

	NdisMoveMemory(&Count, pTmp, sizeof(USHORT));
    Count = cpu2le16(Count);		

    pTmp   += sizeof(USHORT);/*pointer to AKM cipher */

#ifdef DOT11_SAE_SUPPORT
	if (APAuthMode == Ndis802_11AuthModeWPA3PSK || APAuthMode == Ndis802_11AuthModeWPA2PSKWPA3PSK) {

		if (!iswpa2)
			return FALSE;

		if (NdisEqualMemory(sta_akm, &OUI_WPA2_AKM_SAE_SHA256, 4)) {
			pEntry->AuthMode = Ndis802_11AuthModeWPA3PSK;
			pEntry->key_deri_alg = SEC_KEY_DERI_SHA256;
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_USE_SHA256);
			return TRUE;
		} else if (NdisEqualMemory(sta_akm, &OUI_WPA2_PSK_SHA256, 4)) {
			pEntry->AuthMode = Ndis802_11AuthModeWPA2PSK;
			pEntry->key_deri_alg = SEC_KEY_DERI_SHA256;
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_USE_SHA256);
			return TRUE;
		} else if (NdisEqualMemory(sta_akm, &OUI_WPA2_PSK_AKM, 4)) {
			pEntry->AuthMode = Ndis802_11AuthModeWPA2PSK;
			pEntry->key_deri_alg = SEC_KEY_DERI_SHA1;
			CLIENT_STATUS_CLEAR_FLAG(pEntry, fCLIENT_STATUS_USE_SHA256);
			return TRUE;
		}

	} else
#endif
#ifdef CONFIG_OWE_SUPPORT
	if (APAuthMode == Ndis802_11AuthModeOWE) {
		if (!iswpa2)
			return FALSE;

		if (NdisEqualMemory(sta_akm, &OUI_WPA2_AKM_OWE, 4)) {
			pEntry->AuthMode = Ndis802_11AuthModeOWE;
			pEntry->key_deri_alg = SEC_KEY_DERI_SHA256;
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_USE_SHA256);
			return TRUE;
		}
	} else
#endif
	{
		while (Count > 0) {
		/*rtmp_hexdump(RT_DEBUG_TRACE,"MBSS WPA_IE AKM ",pTmp,4); */
			if (RTMPEqualMemory(sta_akm, pTmp, 4))
				return TRUE;

			pTmp += 4;
			Count--;

		}
	}
    return FALSE;/* do not match the AKM */

}


/*
    ==========================================================================
    Description:
       Check sanity of authentication method selector in RSN IE.
    Return:
         TRUE if match
         FALSE otherwise
    ==========================================================================
*/
BOOLEAN RTMPCheckAUTH(
    IN PRTMP_ADAPTER    pAd,
    IN PEID_STRUCT      eid_ptr,
    IN MAC_TABLE_ENTRY	*pEntry)
{
	PUCHAR pStaTmp;
	USHORT Count;	
	UCHAR 	apidx;

	ASSERT(pEntry);
	ASSERT(pEntry->func_tb_idx < pAd->ApCfg.BssidNum);

	apidx = pEntry->func_tb_idx;

#ifndef CONFIG_SECURITY_IMPROVEMENT_SUPPORT
	if (eid_ptr->Len < 16) {
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("RTMPCheckAUTH ==> WPAIE len is too short(%d) \n", eid_ptr->Len));
		return FALSE;
	}
#endif
	/* Store STA RSN_IE capability */
	pStaTmp = (PUCHAR)&eid_ptr->Octet[0];
	if(eid_ptr->Eid == IE_WPA2)
	{
		/* skip Version(2),Multicast cipter(4) 2+4==6 */
		/* point to number of unicast */
        pStaTmp +=6;
	}
	else if (eid_ptr->Eid == IE_WPA)	
	{
		/* skip OUI(4),Vesrion(2),Multicast cipher(4) 4+2+4==10 */
		/* point to number of unicast */
        pStaTmp += 10;
	}
	else
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("RTMPCheckAUTH ==> Unknown WPAIE, WPAIE=%d\n", eid_ptr->Eid));
	    return FALSE;
	}

	/* Store unicast cipher count */
    NdisMoveMemory(&Count, pStaTmp, sizeof(USHORT));
    Count = cpu2le16(Count);		

	/* pointer to unicast cipher */
    pStaTmp += sizeof(USHORT);	

    /* Skip all unicast cipher suite */
    while (Count > 0)
    {
		/* Skip OUI */
		pStaTmp += 4;
		Count--;
	}

	/* Store AKM count */
	NdisMoveMemory(&Count, pStaTmp, sizeof(USHORT));
    Count = cpu2le16(Count);		

	/*pointer to AKM cipher */
    pStaTmp += sizeof(USHORT);			

    if (eid_ptr->Len >= 16)
    {
    	if (eid_ptr->Eid == IE_WPA)
    	{
			while (Count > 0)
			{
				if (RTMPCheckAKM(pStaTmp, &pAd->ApCfg.MBSSID[apidx].RSN_IE[0][0], 0
#if defined(DOT11_SAE_SUPPORT) || defined(CONFIG_OWE_SUPPORT)
					, pAd->ApCfg.MBSSID[apidx].wdev.AuthMode, pEntry
#endif
					))
					return TRUE;
	
				pStaTmp += 4;
				Count--;
			}
    	}
    	else if (eid_ptr->Eid == IE_WPA2)
    	{
    		UCHAR	IE_Idx = 0;

			/* When WPA1/WPA2 mix mode, the RSN_IE is stored in different structure */
			if ((pAd->ApCfg.MBSSID[apidx].wdev.AuthMode == Ndis802_11AuthModeWPA1WPA2) || 
				(pAd->ApCfg.MBSSID[apidx].wdev.AuthMode == Ndis802_11AuthModeWPA1PSKWPA2PSK))
				IE_Idx = 1;

			while (Count > 0) {
				if (RTMPCheckAKM(pStaTmp, &pAd->ApCfg.MBSSID[apidx].RSN_IE[IE_Idx][0], 1
#if defined(DOT11_SAE_SUPPORT) || defined(CONFIG_OWE_SUPPORT)
					, pAd->ApCfg.MBSSID[apidx].wdev.AuthMode, pEntry
#endif
					)) {
#ifdef CONFIG_OWE_SUPPORT
					if (pEntry->AuthMode == Ndis802_11AuthModeOWE) {
						pEntry->key_deri_alg = SEC_KEY_DERI_SHA256;
					/* OWE cannot derive key_deri_alg by OWE akm.
					* it shall parse ECDH parameter to determine it.
					* set a temporary alg here.
					*/
					}
#endif
					return TRUE;
				}

				pStaTmp += 4;
				Count--;
			}
    	}
    }

    return FALSE;
}

#if defined(CONFIG_SECURITY_IMPROVEMENT_SUPPORT) || defined(APCLI_SECURITY_IMPROVEMENT_SUPPORT)

static BOOLEAN wpa_check_pmkid(
	IN PUINT8 rsnie_ptr,
	IN UINT rsnie_len,
	IN NDIS_802_11_AUTHENTICATION_MODE StaAuthMode)
{
	UINT8 count = 0;
	PUINT8 pBuf = NULL;

	if (StaAuthMode == Ndis802_11AuthModeWPA2PSK) {
		pBuf = WPA_ExtractSuiteFromRSNIE(rsnie_ptr, rsnie_len, PMKID_LIST, &count);

		if (count > 0)
			return FALSE;
	}

	return TRUE;
}


/* Ellis: not ready for WAPI */
BOOLEAN wpa_rsne_sanity(
	IN PUCHAR rsnie_ptr,
	IN UCHAR rsnie_len,
	OUT UCHAR *end_field)
{
	EID_STRUCT  *eid_ptr;
	PUCHAR pStaTmp;
	USHORT ver;
	USHORT Count;
	UCHAR len = 0;

	eid_ptr = (EID_STRUCT *)rsnie_ptr;

	if ((eid_ptr->Len + 2) != rsnie_len) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s : the len is invalid !!!\n", __func__));
		return FALSE;
	}

	if (eid_ptr->Len < MIN_LEN_OF_RSNIE) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s : len is too short(len = %d) !!!\n", __func__, eid_ptr->Len));
		return FALSE;
	}

	/* Store STA RSN_IE capability */
	pStaTmp = (PUCHAR)&eid_ptr->Octet[0];

	/* check Element ID */
	if (eid_ptr->Eid == IE_WPA2)
		;
	else if (eid_ptr->Eid == IE_WPA) {
		/* skip OUI(4) */
		pStaTmp += 4;
		len += 4;
	} else if (eid_ptr->Eid == IE_WAPI)
		return TRUE;
	else {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s : invalid IE=%d\n", __func__, eid_ptr->Eid));
		return FALSE;
	}

	/* check version */
	len += 2;
	if (eid_ptr->Len < len) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s : no version(len = %d)\n", __func__, eid_ptr->Len));
		return FALSE;
	}
	NdisMoveMemory(&ver, pStaTmp, sizeof(ver));
	ver = cpu2le16(ver);

	if (ver != 1) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s : unknown version(%d)\n", __func__, ver));
		return FALSE;
	}

	if (eid_ptr->Len == len) {
		/* None of the optional fields are included in the RSNE */
		*end_field = RSN_FIELD_NONE;
		return TRUE;
	}

	pStaTmp += sizeof(USHORT);

	/* check group cipher suite */
	len += LEN_OUI_SUITE;
	if (eid_ptr->Len < len) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s : group cipher is truncated(len = %d)\n", __func__, eid_ptr->Len));
		return FALSE;
	} else if (eid_ptr->Len == len) {
		/* Group Data Cipher Suite are included in the RSNE */
		*end_field = RSN_FIELD_GROUP_CIPHER;
		return TRUE;
	}

	pStaTmp += LEN_OUI_SUITE;

	/* check pairwise cipher suite */
	len += 2;
	if (eid_ptr->Len < len) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s :  pairwise cipher suite cnt is truncated(%d)\n", __func__, eid_ptr->Len));
		return FALSE;
	}

	/* Store pairwise cipher count */
	NdisMoveMemory(&Count, pStaTmp, sizeof(USHORT));
	Count = cpu2le16(Count);

	len += Count * LEN_OUI_SUITE;

	if (eid_ptr->Len < len) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s : Pairwise Cipher Suite is truncated(len %d)\n", __func__, eid_ptr->Len));
		return FALSE;
	} else if (eid_ptr->Len == len) {
		/* Pairwise Cipher Suite are included in the RSNE */
		*end_field = RSN_FIELD_PAIRWISE_CIPHER;
		return TRUE;
	}

	pStaTmp += sizeof(USHORT) + Count * LEN_OUI_SUITE;

	/* check akm suite */
	len += 2;
	if (eid_ptr->Len < len) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s :  akm suite cnt is truncated(%d)\n", __func__, eid_ptr->Len));
		return FALSE;
	}

	/* Store akm count */
	NdisMoveMemory(&Count, pStaTmp, sizeof(USHORT));
	Count = cpu2le16(Count);

	len += Count * LEN_OUI_SUITE;
	if (eid_ptr->Len < len) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s : Akm Suite is truncated(len %d)\n", __func__, eid_ptr->Len));
		return FALSE;
	} else if (eid_ptr->Len == len) {
		/* akm Suite are included in the RSNE */
		*end_field = RSN_FIELD_AKM;
		return TRUE;
	}

	pStaTmp += sizeof(USHORT) + Count * LEN_OUI_SUITE;

	/* check rsn capabilities */
	len += 2;
	if (eid_ptr->Len < len) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s : RSN capabilities is truncated(len %d)\n", __func__, eid_ptr->Len));
		return FALSE;
	} else if (eid_ptr->Len == len) {
		/* rsn capabilities are included in the RSNE */
		*end_field = RSN_FIELD_RSN_CAP;
		return TRUE;
	}

	pStaTmp += 2;

	/* check PMKID */
	len += 2;
	if (eid_ptr->Len < len) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s :  pmkid cnt is truncated(%d)\n", __func__, eid_ptr->Len));
		return FALSE;
	}

	/* Store pmkid count */
	NdisMoveMemory(&Count, pStaTmp, sizeof(USHORT));
	Count = cpu2le16(Count);

	len += Count * 16;

	if (eid_ptr->Len < len) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s : PMKID is truncated(len %d)\n", __func__, eid_ptr->Len));
		return FALSE;
	} else if (eid_ptr->Len == len) {
		/* PMKID are included in the RSNE */
		*end_field = RSN_FIELD_PMKID;
		return TRUE;
	}

	pStaTmp += 2 + Count * 16;


#ifdef DOT11W_PMF_SUPPORT
	/* check Group Management Cipher Suite*/
	len += LEN_OUI_SUITE;
	if (eid_ptr->Len < len) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s : Group Management Cipher Suite is truncated(len %d)\n", __func__, eid_ptr->Len));
		return FALSE;
	} else if (eid_ptr->Len == len) {
		/* PMKID are included in the RSNE */
		*end_field = RSN_FIELD_GROUP_MGMT_CIPHER;
		return TRUE;
	}

	pStaTmp += LEN_OUI_SUITE;
#endif

	MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_TRACE,
			("%s : extensible element len %d\n", __func__, eid_ptr->Len - len));
	*end_field = RSN_FIELD_EXTENSIBLE_ELE;
	return TRUE;
}

#endif

/*
    ==========================================================================
    Description:
       Check validity of the received RSNIE.

    Return:
		status code
    ==========================================================================
*/
UINT	APValidateRSNIE(
	IN PRTMP_ADAPTER    pAd,
	IN PMAC_TABLE_ENTRY pEntry,
	IN PUCHAR			pRsnIe,
	IN UCHAR			rsnie_len)
{
	UINT StatusCode = MLME_SUCCESS;
	PEID_STRUCT  eid_ptr;
#ifdef DOT11W_PMF_SUPPORT
	INT	apidx;
	BSS_STRUCT *pMbss;
#endif /* DOT11W_PMF_SUPPORT */

#ifdef CONFIG_SECURITY_IMPROVEMENT_SUPPORT
	UCHAR end_field = 0;
#endif
	if (rsnie_len == 0)
		return MLME_SUCCESS;

	eid_ptr = (PEID_STRUCT)pRsnIe;
#ifndef CONFIG_SECURITY_IMPROVEMENT_SUPPORT
	if ((eid_ptr->Len + 2) != rsnie_len)
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("[ERROR]APValidateRSNIE : the len is invalid !!!\n"));
		return MLME_UNSPECIFY_FAIL;
	}
#else
	if (wpa_rsne_sanity(pRsnIe, rsnie_len, &end_field) == FALSE) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("%s : wpa_rsne_sanity fail\n", __func__));
		return MLME_UNSPECIFY_FAIL;
	}
#endif
#ifdef DOT11W_PMF_SUPPORT
	apidx = pEntry->func_tb_idx;
	pMbss = &pAd->ApCfg.MBSSID[apidx];
#endif /* DOT11W_PMF_SUPPORT */

#ifdef WAPI_SUPPORT
	if (eid_ptr->Eid == IE_WAPI)
		return MLME_SUCCESS;
#endif /* WAPI_SUPPORT */

#ifndef CONFIG_SECURITY_IMPROVEMENT_SUPPORT
	/* check group cipher */
	if (!RTMPCheckMcast(pAd, eid_ptr, pEntry))
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("[ERROR]APValidateRSNIE : invalid group cipher !!!\n"));	    
	    StatusCode = MLME_INVALID_GROUP_CIPHER;
	}  
	/* Check pairwise cipher */
	else if (!RTMPCheckUcast(pAd, eid_ptr, pEntry))
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("[ERROR]APValidateRSNIE : invalid pairwise cipher !!!\n"));	    
	    StatusCode = MLME_INVALID_PAIRWISE_CIPHER;
	}        
	/* Check AKM */
	else if (!RTMPCheckAUTH(pAd, eid_ptr, pEntry))
	{
	    MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("[ERROR]APValidateRSNIE : invalid AKM !!!\n"));
	    StatusCode = MLME_INVALID_AKMP;
	}                        
#ifdef DOT11W_PMF_SUPPORT
	else if (PMF_RsnCapableValidation(pAd, pRsnIe, rsnie_len, 
									pMbss->PmfCfg.MFPC, pMbss->PmfCfg.MFPR,
#ifdef CONFIG_SECURITY_IMPROVEMENT_SUPPORT
									end_field,
#endif
#ifdef CONFIG_OWE_SUPPORT
					apidx,
#endif
					 pEntry) != PMF_STATUS_SUCCESS) {
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[PMF]%s : Invalid PMF Capability !!!\n", __func__));
		StatusCode = MLME_ROBUST_MGMT_POLICY_VIOLATION;
	}
#endif /* DOT11W_PMF_SUPPORT */

#else
		/* check group cipher */
		if (end_field >= RSN_FIELD_GROUP_CIPHER && !RTMPCheckMcast(pAd, eid_ptr, pEntry)) {
			MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
				("[ERROR]APValidateRSNIE : invalid group cipher !!!\n"));
			StatusCode = MLME_INVALID_GROUP_CIPHER;
		} else if (end_field < RSN_FIELD_GROUP_CIPHER
			&& pMbss->wdev.GroupKeyWepStatus == Ndis802_11AESEnable)
			pEntry->GroupKeyWepStatus = pMbss->wdev.GroupKeyWepStatus;
		else if (end_field < RSN_FIELD_GROUP_CIPHER) {
			MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
				("[ERROR]%s : invalid group cipher(peer use default cipher) !!!\n", __func__));
			return MLME_INVALID_GROUP_CIPHER;
		}

		/* Check pairwise cipher */
		if (end_field >= RSN_FIELD_PAIRWISE_CIPHER && !RTMPCheckUcast(pAd, eid_ptr, pEntry)) {
			MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
				("[ERROR]APValidateRSNIE : invalid pairwise cipher !!!\n"));
			StatusCode = MLME_INVALID_PAIRWISE_CIPHER;
		} else if (end_field < RSN_FIELD_PAIRWISE_CIPHER
				&& pMbss->wdev.WepStatus == Ndis802_11AESEnable)
			pEntry->WepStatus = pMbss->wdev.WepStatus;
		else if (end_field < RSN_FIELD_PAIRWISE_CIPHER) {
			MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
				("[ERROR]%s : invalid pairwise cipher(peer use default cipher) !!!\n", __func__));
			return MLME_INVALID_PAIRWISE_CIPHER;
		}

		/* Check AKM */
		if (end_field >= RSN_FIELD_AKM && !RTMPCheckAUTH(pAd, eid_ptr, pEntry)) {
			MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
				("[ERROR]APValidateRSNIE : invalid AKM !!!\n"));
			StatusCode = MLME_INVALID_AKMP;
		} else if (end_field < RSN_FIELD_AKM
			&& pMbss->wdev.AuthMode == Ndis802_11AuthModeWPA2)
			pEntry->AuthMode = pMbss->wdev.AuthMode;
		else if (end_field < RSN_FIELD_AKM) {
			MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("[ERROR]%s : invalid AKM(peer use default akm) !!!\n", __func__));
			return MLME_INVALID_AKMP;
		}
		/* Check PMKID */
		if (end_field >= RSN_FIELD_PMKID && !wpa_check_pmkid(pRsnIe, rsnie_len, pEntry->AuthMode)) {
			MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
				("[ERROR]%s : invalid PMKID !!!\n", __func__));
			return MLME_UNSPECIFY_FAIL;
		}
#ifdef DOT11W_PMF_SUPPORT
		else if (PMF_RsnCapableValidation(pAd, pRsnIe, rsnie_len,
					pMbss->PmfCfg.MFPC, pMbss->PmfCfg.MFPR,
#ifdef CONFIG_SECURITY_IMPROVEMENT_SUPPORT
					end_field,
#endif
#ifdef CONFIG_OWE_SUPPORT
					apidx,
#endif
					pEntry) != PMF_STATUS_SUCCESS) {
			MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
				("[PMF]%s : Invalid PMF Capability !!!\n", __func__));
			StatusCode = MLME_ROBUST_MGMT_POLICY_VIOLATION;
		}
#endif /* DOT11W_PMF_SUPPORT */


#endif/* CONFIG_SECURITY_IMPROVEMENT_SUPPORT */

	if (StatusCode != MLME_SUCCESS)
	{
		/* send wireless event - for RSN IE sanity check fail */
		RTMPSendWirelessEvent(pAd, IW_RSNIE_SANITY_FAIL_EVENT_FLAG, pEntry->Addr, 0, 0);
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("%s : invalid status code(%d) !!!\n", __FUNCTION__, StatusCode));
	}
	else
	{
        UCHAR CipherAlg = CIPHER_NONE;
		
        if (pEntry->WepStatus == Ndis802_11WEPEnabled)
            CipherAlg = CIPHER_WEP64;
        else if (pEntry->WepStatus == Ndis802_11TKIPEnable)
            CipherAlg = CIPHER_TKIP;
        else if (pEntry->WepStatus == Ndis802_11AESEnable)
            CipherAlg = CIPHER_AES;
        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("%s : (AID#%d WepStatus=%s)\n", __FUNCTION__, pEntry->Aid, CipherName[CipherAlg]));
	}

	
	
	return StatusCode;

}

/*
    ==========================================================================
    Description:
        Function to handle countermeasures active attack.  Init 60-sec timer if necessary.
    Return:
    ==========================================================================
*/
VOID HandleCounterMeasure(RTMP_ADAPTER *pAd, MAC_TABLE_ENTRY *pEntry)
{
	INT i;
	BOOLEAN Cancelled;

	if (!pEntry)
		return;

	/* Todo by AlbertY - Not support currently in ApClient-link */
	if (IS_ENTRY_APCLI(pEntry))
		return;

	/* if entry not set key done, ignore this RX MIC ERROR */
	if ((pEntry->WpaState < AS_PTKINITDONE) || (pEntry->GTKState != REKEY_ESTABLISHED))
		return;

	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("HandleCounterMeasure ===> \n"));

	/* record which entry causes this MIC error, if this entry sends disauth/disassoc, AP doesn't need to log the CM */
	pEntry->CMTimerRunning = TRUE;
	pAd->ApCfg.MICFailureCounter++;

	/* send wireless event - for MIC error */
	RTMPSendWirelessEvent(pAd, IW_MIC_ERROR_EVENT_FLAG, pEntry->Addr, 0, 0); 

	if (pAd->ApCfg.CMTimerRunning == TRUE)
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("Receive CM Attack Twice within 60 seconds ====>>> \n"));

		/* send wireless event - for counter measures */
		RTMPSendWirelessEvent(pAd, IW_COUNTER_MEASURES_EVENT_FLAG, pEntry->Addr, 0, 0); 
		ApLogEvent(pAd, pEntry->Addr, EVENT_COUNTER_M);

		/* renew GTK */
		GenRandom(pAd, pAd->ApCfg.MBSSID[pEntry->func_tb_idx].wdev.bssid, pAd->ApCfg.MBSSID[pEntry->func_tb_idx].GNonce);

		/* Cancel CounterMeasure Timer */
		RTMPCancelTimer(&pAd->ApCfg.CounterMeasureTimer, &Cancelled);
		pAd->ApCfg.CMTimerRunning = FALSE;

		for (i = 0; i < MAX_LEN_OF_MAC_TABLE; i++)
		{
			/* happened twice within 60 sec,  AP SENDS disaccociate all associated STAs.  All STA's transition to State 2 */
			if (IS_ENTRY_CLIENT(&pAd->MacTab.Content[i]))
			{
				MlmeDeAuthAction(pAd, &pAd->MacTab.Content[i], REASON_MIC_FAILURE, FALSE);
			}
		}

		/*
			Further,  ban all Class 3 DATA transportation for a period 0f 60 sec
			disallow new association , too 
		*/
		pAd->ApCfg.BANClass3Data = TRUE;

		/* check how many entry left...  should be zero */
		/*pAd->ApCfg.MBSSID[pEntry->func_tb_idx].GKeyDoneStations = pAd->MacTab.Size; */
		/*MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("GKeyDoneStations=%d \n", pAd->ApCfg.MBSSID[pEntry->func_tb_idx].GKeyDoneStations)); */
	}

	RTMPSetTimer(&pAd->ApCfg.CounterMeasureTimer, 60 * MLME_TASK_EXEC_INTV * MLME_TASK_EXEC_MULTIPLE);
	pAd->ApCfg.CMTimerRunning = TRUE;
	pAd->ApCfg.PrevaMICFailTime = pAd->ApCfg.aMICFailTime;
	RTMP_GetCurrentSystemTime(&pAd->ApCfg.aMICFailTime);
}


/*
    ==========================================================================
    Description:
        countermeasures active attack timer execution
    Return:
    ==========================================================================
*/
VOID CMTimerExec(
    IN PVOID SystemSpecific1, 
    IN PVOID FunctionContext, 
    IN PVOID SystemSpecific2, 
    IN PVOID SystemSpecific3) 
{
    UINT            i,j=0;
    PRTMP_ADAPTER   pAd = (PRTMP_ADAPTER)FunctionContext;
        
    pAd->ApCfg.BANClass3Data = FALSE;
    for (i = 0; i < MAX_LEN_OF_MAC_TABLE; i++)
    {
        if (IS_ENTRY_CLIENT(&pAd->MacTab.Content[i])
		&& (pAd->MacTab.Content[i].CMTimerRunning == TRUE))
        {
            pAd->MacTab.Content[i].CMTimerRunning =FALSE;
            j++;
        }
    }

    if (j > 1)
        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("Find more than one entry which generated MIC Fail ..  \n"));

    pAd->ApCfg.CMTimerRunning = FALSE;
}


VOID WPARetryExec(
    IN PVOID SystemSpecific1, 
    IN PVOID FunctionContext, 
    IN PVOID SystemSpecific2, 
    IN PVOID SystemSpecific3) 
{
    MAC_TABLE_ENTRY     *pEntry = (MAC_TABLE_ENTRY *)FunctionContext;

    if ((pEntry) && IS_ENTRY_CLIENT(pEntry))
    {
        PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pEntry->pAd;
        
        pEntry->ReTryCounter++;
        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("WPARetryExec---> ReTryCounter=%d, WpaState=%d \n", pEntry->ReTryCounter, pEntry->WpaState));

        switch (pEntry->AuthMode)
        {
			case Ndis802_11AuthModeWPA:
            case Ndis802_11AuthModeWPAPSK:
			case Ndis802_11AuthModeWPA2:
            case Ndis802_11AuthModeWPA2PSK:
				
				/* 1. GTK already retried, give up and disconnect client. */
				if (pEntry->ReTryCounter > (GROUP_MSG1_RETRY_TIMER_CTR + 3))
				{	 
					/* send wireless event - for group key handshaking timeout */
					RTMPSendWirelessEvent(pAd, IW_GROUP_HS_TIMEOUT_EVENT_FLAG, pEntry->Addr, pEntry->wdev->wdev_idx, 0); 
					
					MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("WPARetryExec::Group Key HS exceed retry count, Disassociate client, pEntry->ReTryCounter %d\n", pEntry->ReTryCounter));
					MlmeDeAuthAction(pAd, pEntry, REASON_GROUP_KEY_HS_TIMEOUT, FALSE);
				}
				/* 2. Retry GTK. */
				else if (pEntry->ReTryCounter > GROUP_MSG1_RETRY_TIMER_CTR)
				{
					MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("WPARetryExec::ReTry 2-way group-key Handshake \n"));
					if (pEntry->GTKState == REKEY_NEGOTIATING)
					{
						WPAStart2WayGroupHS(pAd, pEntry);
					}
				}
				/* 3. 4-way message 3 retried more than three times. Disconnect client */
				else if (pEntry->ReTryCounter > (PEER_MSG3_RETRY_TIMER_CTR + 3))
				{			  
					/* send wireless event - for pairwise key handshaking timeout */
					RTMPSendWirelessEvent(pAd, IW_PAIRWISE_HS_TIMEOUT_EVENT_FLAG, pEntry->Addr, pEntry->apidx, 0);
					
					MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("WPARetryExec::MSG3 timeout, pEntry->ReTryCounter = %d\n", pEntry->ReTryCounter));
					MlmeDeAuthAction(pAd, pEntry, REASON_4_WAY_TIMEOUT, FALSE);
					
				}
				/* 4. Retry 4 way message 3 */
				else if (pEntry->ReTryCounter >= PEER_MSG3_RETRY_TIMER_CTR)
				{			  
					MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("WPARetryExec::ReTry MSG3 of 4-way Handshake\n"));
					WPAPairMsg3Retry(pAd, pEntry, PEER_MSG1_RETRY_EXEC_INTV);					 
				}
				/* 5. 4-way message 1 retried more than three times. Disconnect client */
				else if (pEntry->ReTryCounter > (PEER_MSG1_RETRY_TIMER_CTR + 3))
				{
					/* send wireless event - for pairwise key handshaking timeout */
					RTMPSendWirelessEvent(pAd, IW_PAIRWISE_HS_TIMEOUT_EVENT_FLAG, pEntry->Addr, pEntry->wdev->wdev_idx, 0);
			
					MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("WPARetryExec::MSG1 timeout, pEntry->ReTryCounter = %d\n", pEntry->ReTryCounter));
					MlmeDeAuthAction(pAd, pEntry, REASON_4_WAY_TIMEOUT, FALSE);
				}
				/* 6. Retry 4 way message 1, the last try, the timeout is 3 sec for EAPOL-Start */
				else if (pEntry->ReTryCounter == (PEER_MSG1_RETRY_TIMER_CTR + 3))				 
				{
					MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("WPARetryExec::Retry MSG1, the last try\n"));
					WPAStart4WayHS(pAd , pEntry, PEER_MSG3_RETRY_EXEC_INTV);
				}
				/* 7. Retry 4 way message 1 */
				else if (pEntry->ReTryCounter < (PEER_MSG1_RETRY_TIMER_CTR + 3))
				{
					if ((pEntry->WpaState == AS_PTKSTART) || (pEntry->WpaState == AS_INITPSK) || (pEntry->WpaState == AS_INITPMK))
					{
						MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("WPARetryExec::ReTry MSG1 of 4-way Handshake\n"));
						WPAStart4WayHS(pAd, pEntry, PEER_MSG1_RETRY_EXEC_INTV);
					}
				}
					break;
			
			default:
				break;
		
				
        }
    }
#ifdef APCLI_SUPPORT	
	else if ((pEntry) && IS_ENTRY_APCLI(pEntry))
	{
		if (pEntry->AuthMode == Ndis802_11AuthModeWPA || pEntry->AuthMode == Ndis802_11AuthModeWPAPSK)
		{						
			PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pEntry->pAd;

			if (pEntry->func_tb_idx < MAX_APCLI_NUM)
			{		
				UCHAR ifIndex = pEntry->func_tb_idx;
								
				MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("(%s) ApCli interface[%d] startdown.\n", __FUNCTION__, ifIndex));
#ifdef MAC_REPEATER_SUPPORT
				if ((pEntry->bReptCli) && (pAd->ApCfg.bMACRepeaterEn == TRUE))
					ifIndex = (64 + ifIndex*MAX_EXT_MAC_ADDR_SIZE + pEntry->MatchReptCliIdx);
#endif /* MAC_REPEATER_SUPPORT */
				MlmeEnqueue(pAd, APCLI_CTRL_STATE_MACHINE, APCLI_CTRL_DISCONNECT_REQ, 0, NULL, ifIndex);
#ifdef MAC_REPEATER_SUPPORT
				if ( (pAd->ApCfg.bMACRepeaterEn == TRUE) && (pEntry->bReptCli))
				{
					RTMP_MLME_HANDLER(pAd);
					//RTMPRemoveRepeaterEntry(pAd, pEntry->func_tb_idx, pEntry->MatchReptCliIdx);
				}
#endif /* MAC_REPEATER_SUPPORT */
			}
		}
	}
#endif /* APCLI_SUPPORT */	
}


/*
    ==========================================================================
    Description:
        Timer execution function for periodically updating group key.
    Return:
    ==========================================================================
*/  
VOID GREKEYPeriodicExec(
    IN PVOID SystemSpecific1, 
    IN PVOID FunctionContext, 
    IN PVOID SystemSpecific2, 
    IN PVOID SystemSpecific3) 
{
	UINT i, apidx;
	ULONG temp_counter = 0;
	RTMP_ADAPTER *pAd = (RTMP_ADAPTER *)FunctionContext;
	PRALINK_TIMER_STRUCT pTimer = (PRALINK_TIMER_STRUCT) SystemSpecific3;
	BSS_STRUCT *pMbss = NULL;
	struct wifi_dev *wdev;
    
	for (apidx=0; apidx<pAd->ApCfg.BssidNum; apidx++)
	{
		if (&pAd->ApCfg.MBSSID[apidx].REKEYTimer == pTimer)
			break;
	}
    
	if (apidx == pAd->ApCfg.BssidNum)
		return;
	else
		pMbss = &pAd->ApCfg.MBSSID[apidx];

	wdev = &pMbss->wdev;
	if (wdev->AuthMode < Ndis802_11AuthModeWPA || 
		wdev->AuthMode > Ndis802_11AuthModeWPA1PSKWPA2PSK)
		return;
	
    if ((pMbss->WPAREKEY.ReKeyMethod == TIME_REKEY) && (pMbss->REKEYCOUNTER < 0xffffffff))
        temp_counter = (++pMbss->REKEYCOUNTER);
    /* REKEYCOUNTER is incremented every MCAST packets transmitted, */
    /* But the unit of Rekeyinterval is 1K packets */
    else if (pMbss->WPAREKEY.ReKeyMethod == PKT_REKEY)
        temp_counter = pMbss->REKEYCOUNTER/1000;
    else
    {
		return;
    }
    
	if (temp_counter > (pMbss->WPAREKEY.ReKeyInterval))
    {
        pMbss->REKEYCOUNTER = 0;
		pMbss->RekeyCountDown = 3;
        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("Rekey Interval Excess, GKeyDoneStations=%d\n", pMbss->StaCount));
        
        /* take turn updating different groupkey index, */
		if ((pMbss->StaCount) > 0)
        {
			/* change key index */
			wdev->DefaultKeyId = (wdev->DefaultKeyId == 1) ? 2 : 1;         
	
			/* Generate GNonce randomly */
			GenRandom(pAd, wdev->bssid, pMbss->GNonce);

			/* Update GTK */
			WpaDeriveGTK(pMbss->GMK, 
						(UCHAR*)pMbss->GNonce, 
						wdev->bssid, pMbss->GTK, LEN_TKIP_GTK);
				
			/* Process 2-way handshaking */
            for (i = 0; i < MAX_LEN_OF_MAC_TABLE; i++)
            {
				MAC_TABLE_ENTRY  *pEntry;

				pEntry = &pAd->MacTab.Content[i];
				if (IS_ENTRY_CLIENT(pEntry) && 
					(pEntry->WpaState == AS_PTKINITDONE) &&
						(pEntry->func_tb_idx == apidx))
                {

#ifdef A4_CONN
					if (IS_ENTRY_A4(pEntry))
						continue;
#endif /* A4_CONN */
					pEntry->GTKState = REKEY_NEGOTIATING;
					/*set retry counter for GTK update*/
					pEntry->ReTryCounter = GROUP_MSG1_RETRY_TIMER_CTR;
                	WPAStart2WayGroupHS(pAd, pEntry);
                    MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("Rekey interval excess, Update Group Key for  %x %x %x  %x %x %x , DefaultKeyId= %x \n",\
										PRINT_MAC(pEntry->Addr), wdev->DefaultKeyId));
				}
			}
		}
	}       

	/* Use countdown to ensure the 2-way handshaking had completed */
	if (pMbss->RekeyCountDown > 0)
	{
		pMbss->RekeyCountDown--;
		if (pMbss->RekeyCountDown == 0)
		{
			USHORT	Wcid;

			/* Get a specific WCID to record this MBSS key attribute */
			GET_GroupKey_WCID(pAd, Wcid, apidx);

			/* Install shared key table */
			WPAInstallSharedKey(pAd, 
								wdev->GroupKeyWepStatus, 
								apidx, 
								wdev->DefaultKeyId, 
								Wcid, 
								TRUE, 
								pMbss->GTK,
								LEN_TKIP_GTK);
		}
	}       
    
}


#if defined(DOT1X_SUPPORT) || defined(CONFIG_OWE_SUPPORT) || defined(DOT11_SAE_SUPPORT)
/*
    ========================================================================

    Routine Description:
        Sending EAP Req. frame to station in authenticating state.
        These frames come from Authenticator deamon.

    Arguments:
        pAdapter        Pointer to our adapter
        pPacket     Pointer to outgoing EAP frame body + 8023 Header
        Len             length of pPacket
        
    Return Value:
        None
    ========================================================================
*/
VOID WpaSend(RTMP_ADAPTER *pAdapter, UCHAR *pPacket, ULONG Len)
{
	PEAP_HDR pEapHdr;
	UCHAR Addr[MAC_ADDR_LEN];
	UCHAR Header802_3[LENGTH_802_3];
	MAC_TABLE_ENTRY *pEntry;
	STA_TR_ENTRY *tr_entry;
	PUCHAR pData;
    
    
    NdisMoveMemory(Addr, pPacket, 6);
	NdisMoveMemory(Header802_3, pPacket, LENGTH_802_3);
    pEapHdr = (EAP_HDR*)(pPacket + LENGTH_802_3);
	pData = (pPacket + LENGTH_802_3);
	
    if ((pEntry = MacTableLookup(pAdapter, Addr)) == NULL)
    {	
		return;
    }

	tr_entry = &pAdapter->MacTab.tr_entry[pEntry->wcid];
	/* Send EAP frame to STA */
    if (((pEntry->AuthMode >= Ndis802_11AuthModeWPA) && (pEapHdr->ProType != EAPOLKey)) ||
        (pAdapter->ApCfg.MBSSID[pEntry->func_tb_idx].wdev.IEEE8021X == TRUE))
		RTMPToWirelessSta(pAdapter, 
						  pEntry, 
						  Header802_3, 
						  LENGTH_802_3, 
						  pData, 
						  Len - LENGTH_802_3, 
						  (tr_entry->PortSecured == WPA_802_1X_PORT_SECURED) ? FALSE : TRUE);		
	
	
    if (RTMPEqualMemory((pPacket+12), EAPOL, 2))
    {
        switch (pEapHdr->code)
        {
			case EAP_CODE_REQUEST:
				if ((pEntry->WpaState >= AS_PTKINITDONE) && (pEapHdr->ProType == EAPPacket))
				{
					pEntry->WpaState = AS_AUTHENTICATION;
					MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("Start to re-authentication by 802.1x daemon\n"));					
				}
				break;

			/* After receiving EAP_SUCCESS, trigger state machine */	
            case EAP_CODE_SUCCESS:
                if ((pEntry->AuthMode >= Ndis802_11AuthModeWPA) && (pEapHdr->ProType != EAPOLKey))
                {
                    MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE,("Send EAP_CODE_SUCCESS\n\n"));
                    if (pEntry->Sst == SST_ASSOC)
                    {
                        pEntry->WpaState = AS_INITPMK;
						/* Only set the expire and counters */
	                    pEntry->ReTryCounter = PEER_MSG1_RETRY_TIMER_CTR;
	                    WPAStart4WayHS(pAdapter, pEntry, PEER_MSG1_RETRY_EXEC_INTV);
                    }
                }
                else
                {
                    pEntry->PrivacyFilter = Ndis802_11PrivFilterAcceptAll;
                    pEntry->WpaState = AS_PTKINITDONE;
                    pAdapter->ApCfg.MBSSID[pEntry->func_tb_idx].wdev.PortSecured = WPA_802_1X_PORT_SECURED;
                    tr_entry->PortSecured = WPA_802_1X_PORT_SECURED;
#ifdef WSC_AP_SUPPORT
                    if (pAdapter->ApCfg.MBSSID[pEntry->func_tb_idx].WscControl.WscConfMode != WSC_DISABLE)
                        WscInformFromWPA(pEntry);
#endif /* WSC_AP_SUPPORT */
                    MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE,("IEEE8021X-WEP : Send EAP_CODE_SUCCESS\n\n"));
                }
                break;

            case EAP_CODE_FAILURE:
                break;

            default:
                break;    
        }
    }
    else     
    {
        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("Send Deauth, Reason : REASON_NO_LONGER_VALID\n"));
        MlmeDeAuthAction(pAdapter, pEntry, REASON_NO_LONGER_VALID, FALSE);
    }
}    

#if defined(CONFIG_OWE_SUPPORT) || defined(DOT11_SAE_SUPPORT)
VOID store_pmkid_cache_in_entry(
	IN RTMP_ADAPTER * pAd,
	IN MAC_TABLE_ENTRY * pEntry,
	IN INT32 cache_idx)
{
	if (!pEntry) {
		MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
			("%s(): pEntry is null\n", __func__));
		return;
	}

	MTWF_LOG(DBG_CAT_SEC, DBG_SUBCAT_ALL, DBG_LVL_LOUD, ("EntryType = %d\n", pEntry->EntryType));

	if (cache_idx == -1) {
		pEntry->pmkid = NULL;
		pEntry->pmk_cache = NULL;
	} else {
		if (IS_ENTRY_CLIENT(pEntry)) {
#ifdef CONFIG_AP_SUPPORT
			BSS_STRUCT *pBMbss;

			pBMbss = &pAd->ApCfg.MBSSID[pEntry->func_tb_idx];
			pEntry->pmkid = &pBMbss->PMKIDCache.BSSIDInfo[cache_idx].PMKID[0];
			pEntry->pmk_cache = pBMbss->PMKIDCache.BSSIDInfo[cache_idx].PMK;
#endif
		}
	}

}


UCHAR is_pmkid_cache_in_entry(
	IN MAC_TABLE_ENTRY * pEntry)
{
	if (pEntry && pEntry->pmkid && pEntry->pmk_cache)
		return TRUE;
	else
		return FALSE;
}
#endif
VOID RTMPAddPMKIDCache(
	IN  PRTMP_ADAPTER   		pAd,
	IN	INT						apidx,
	IN	PUCHAR				pAddr,
	IN	UCHAR					*PMKID,
	IN	UCHAR					*PMK
#ifdef CONFIG_OWE_SUPPORT
	, IN UINT8  pmk_len
#endif
	)
{
	INT	i, CacheIdx;

	/* Update PMKID status */
	if ((CacheIdx = RTMPSearchPMKIDCache(pAd, apidx, pAddr)) != -1)
	{
		NdisGetSystemUpTime(&(pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[CacheIdx].RefreshTime));
		NdisMoveMemory(&pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[CacheIdx].PMKID, PMKID, LEN_PMKID);
		NdisMoveMemory(&pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[CacheIdx].PMK, PMK,
#ifndef CONFIG_OWE_SUPPORT
				LEN_PMK
#else
				pmk_len
#endif
				);

		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, 
					("RTMPAddPMKIDCache update %02x:%02x:%02x:%02x:%02x:%02x cache(%d) from IF(ra%d)\n", 
					PRINT_MAC(pAddr), CacheIdx, apidx));
		
		return;
	}

	/* Add a new PMKID */
	for (i = 0; i < MAX_PMKID_COUNT; i++)
	{
		if (!pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[i].Valid)
		{
			pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[i].Valid = TRUE;
			NdisGetSystemUpTime(&(pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[i].RefreshTime));
			COPY_MAC_ADDR(&pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[i].MAC, pAddr);
			NdisMoveMemory(&pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[i].PMKID, PMKID, LEN_PMKID);
			NdisMoveMemory(&pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[i].PMK, PMK,
#ifndef CONFIG_OWE_SUPPORT
					LEN_PMK
#else
					pmk_len
#endif
					);

			MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("RTMPAddPMKIDCache add %02x:%02x:%02x:%02x:%02x:%02x cache(%d) from IF(ra%d)\n", 
            					PRINT_MAC(pAddr), i, apidx));
			break;
		}
	}
 
	if (i == MAX_PMKID_COUNT)
	{
		ULONG	timestamp = 0, idx = 0;

		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("RTMPAddPMKIDCache(IF(%d) Cache full\n", apidx));
		for (i = 0; i < MAX_PMKID_COUNT; i++)
		{
			if (pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[i].Valid)
			{
				if (((timestamp == 0) && (idx == 0)) || ((timestamp != 0) && timestamp < pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[i].RefreshTime))
				{
					timestamp = pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[i].RefreshTime;
					idx = i;
				}
			}
		}
		pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[idx].Valid = TRUE;
		NdisGetSystemUpTime(&(pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[idx].RefreshTime));
		COPY_MAC_ADDR(&pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[idx].MAC, pAddr);
		NdisMoveMemory(&pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[idx].PMKID, PMKID, LEN_PMKID);
		NdisMoveMemory(&pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[idx].PMK, PMK,
#ifndef CONFIG_OWE_SUPPORT
				LEN_PMK
#else
				pmk_len
#endif
				);

		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("RTMPAddPMKIDCache add %02x:%02x:%02x:%02x:%02x:%02x cache(%ld) from IF(ra%d)\n", 
           						PRINT_MAC(pAddr), idx, apidx));
	}
}

INT RTMPSearchPMKIDCache(
	IN  PRTMP_ADAPTER   pAd,
	IN	INT				apidx,
	IN	PUCHAR		pAddr)
{
	INT	i = 0;
	
	for (i = 0; i < MAX_PMKID_COUNT; i++)
	{
		if ((pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[i].Valid)
			&& MAC_ADDR_EQUAL(&pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[i].MAC, pAddr))
		{
			MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("RTMPSearchPMKIDCache %02x:%02x:%02x:%02x:%02x:%02x cache(%d) from IF(ra%d)\n", 
            						PRINT_MAC(pAddr), i, apidx));
			break;
		}
	}

	if (i == MAX_PMKID_COUNT)
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("RTMPSearchPMKIDCache - IF(%d) not found\n", apidx));
		return -1;
	}

	return i;
}

VOID RTMPDeletePMKIDCache(
	IN  PRTMP_ADAPTER   pAd,
	IN	INT				apidx,
	IN  INT				idx)
{
	PAP_BSSID_INFO pInfo = &pAd->ApCfg.MBSSID[apidx].PMKIDCache.BSSIDInfo[idx];

	if (pInfo->Valid)
	{
		pInfo->Valid = FALSE;
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("RTMPDeletePMKIDCache(IF(%d), del PMKID CacheIdx=%d\n", apidx, idx));
	}
}

VOID RTMPMaintainPMKIDCache(
	IN  PRTMP_ADAPTER   pAd)
{
	INT	i, j;
	ULONG Now;
	for (i = 0; i < MAX_MBSSID_NUM(pAd); i++)
	{
		BSS_STRUCT *pMbss = &pAd->ApCfg.MBSSID[i];
	
		for (j = 0; j < MAX_PMKID_COUNT; j++)
		{
			PAP_BSSID_INFO pBssInfo = &pMbss->PMKIDCache.BSSIDInfo[j];

			NdisGetSystemUpTime(&Now);

			if ((pBssInfo->Valid)
				&& /*((Now - pBssInfo->RefreshTime) >= pMbss->PMKCachePeriod)*/
				(RTMP_TIME_AFTER(Now, (pBssInfo->RefreshTime + pMbss->PMKCachePeriod))))
			{
				RTMPDeletePMKIDCache(pAd, i, j);
			}
		}
	}
}
#endif /* DOT1X_SUPPORT */


/*
    ==========================================================================
    Description:
       Set group re-key timer

    Return:
		
    ==========================================================================
*/
VOID	WPA_APSetGroupRekeyAction(
	IN  PRTMP_ADAPTER   pAd)
{
	UINT8	apidx = 0;

	for (apidx = 0; apidx < pAd->ApCfg.BssidNum; apidx++)
	{
		BSS_STRUCT *pMbss = &pAd->ApCfg.MBSSID[apidx];
		struct wifi_dev *wdev = &pMbss->wdev;

		if ((wdev->WepStatus == Ndis802_11TKIPEnable) ||
			(wdev->WepStatus == Ndis802_11AESEnable) ||
			(wdev->WepStatus == Ndis802_11TKIPAESMix))
		{
			/* Group rekey related */
			if ((pMbss->WPAREKEY.ReKeyInterval != 0) 
				&& ((pMbss->WPAREKEY.ReKeyMethod == TIME_REKEY) || (
					pMbss->WPAREKEY.ReKeyMethod == PKT_REKEY))) 
			{
				/* Regularly check the timer */
				if (pMbss->REKEYTimerRunning == FALSE)
				{
					RTMPSetTimer(&pMbss->REKEYTimer, GROUP_KEY_UPDATE_EXEC_INTV);

					pMbss->REKEYTimerRunning = TRUE;
					pMbss->REKEYCOUNTER = 0;
				}
				MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, (" %s : Group rekey method= %ld , interval = 0x%lx\n",
											__FUNCTION__, pMbss->WPAREKEY.ReKeyMethod,
											pMbss->WPAREKEY.ReKeyInterval));
			}
			else
				pMbss->REKEYTimerRunning = FALSE;
		}
	}
}

#ifdef QOS_DLS_SUPPORT
VOID RTMPHandleSTAKey(
    IN PRTMP_ADAPTER    pAd, 
    IN PMAC_TABLE_ENTRY	pEntry,
    IN MLME_QUEUE_ELEM  *Elem) 
{
	extern UCHAR		OUI_WPA2_WEP40[];
	ULONG				FrameLen = 0;
	PUCHAR				pOutBuffer = NULL;
	UCHAR				Header802_3[14];
	UCHAR				*mpool;
	PEAPOL_PACKET		pOutPacket;
	PEAPOL_PACKET		pSTAKey;
	//PHEADER_802_11		pHeader;
	UCHAR				Offset = 0;
	ULONG				MICMsgLen;
	UCHAR				DA[MAC_ADDR_LEN];
	UCHAR				Key_Data[512];
	UCHAR				key_length;
	UCHAR				mic[LEN_KEY_DESC_MIC];
	UCHAR				rcv_mic[LEN_KEY_DESC_MIC];
	UCHAR				digest[80];
	UCHAR				temp[64];
	PMAC_TABLE_ENTRY	pDaEntry;

	/*Benson add for big-endian 20081016--> */
	KEY_INFO			peerKeyInfo;
	/*Benson add 20081016 <-- */
	
	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("==> RTMPHandleSTAKey\n"));

	if (!pEntry)
		return;
	
	if ((pEntry->WpaState != AS_PTKINITDONE))
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("Not expect calling STAKey hand shaking here"));
		return;
	}

    //pHeader = (PHEADER_802_11) Elem->Msg;

	/* QoS control field (2B) is took off */
/*    if (pHeader->FC.SubType & 0x08) */
/*        Offset += 2; */
    
	pSTAKey = (PEAPOL_PACKET)&Elem->Msg[LENGTH_802_11 + LENGTH_802_1_H + Offset];	
	/*Benson add for big-endian 20081016--> */
	NdisZeroMemory((PUCHAR)&peerKeyInfo, sizeof(peerKeyInfo));
	NdisMoveMemory((PUCHAR)&peerKeyInfo, (PUCHAR)&pSTAKey->KeyDesc.KeyInfo, sizeof(KEY_INFO));
	*((USHORT *)&peerKeyInfo) = cpu2le16(*((USHORT *)&peerKeyInfo));
	/*Benson add 20081016 <-- */
	
    /* Check Replay Counter */
    if (!RTMPEqualMemory(pSTAKey->KeyDesc.ReplayCounter, pEntry->R_Counter, LEN_KEY_DESC_REPLAY))
    {
        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("Replay Counter Different in STAKey handshake!! \n"));
        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("Receive : %d %d %d %d  \n",
				pSTAKey->KeyDesc.ReplayCounter[0],
				pSTAKey->KeyDesc.ReplayCounter[1],
				pSTAKey->KeyDesc.ReplayCounter[2],
				pSTAKey->KeyDesc.ReplayCounter[3]));
        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("Current : %d %d %d %d  \n",
				pEntry->R_Counter[4],pEntry->R_Counter[5],
				pEntry->R_Counter[6],pEntry->R_Counter[7]));
        return;
    }

    /* Check MIC, if not valid, discard silently */
    NdisMoveMemory(DA, &pSTAKey->KeyDesc.KeyData[6], MAC_ADDR_LEN);

	if (peerKeyInfo.KeyMic && peerKeyInfo.Secure && peerKeyInfo.Request)/*Benson add for big-endian 20081016 --> */
	{
		pEntry->bDlsInit = TRUE;
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("STAKey Initiator: %02x:%02x:%02x:%02x:%02x:%02x\n",
					PRINT_MAC(pEntry->Addr)));
	}


    MICMsgLen = pSTAKey->Body_Len[1] | ((pSTAKey->Body_Len[0]<<8) && 0xff00);
    MICMsgLen += LENGTH_EAPOL_H;
    if (MICMsgLen > (Elem->MsgLen - LENGTH_802_11 - LENGTH_802_1_H))
    {
        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("Receive wrong format EAPOL packets \n"));
        return;        
    }

	/* This is proprietary DLS protocol, it will be adhered when spec. is finished. */
	NdisZeroMemory(temp, 64);
	NdisZeroMemory(pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK, sizeof(pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK));
	NdisMoveMemory(temp, "IEEE802.11 WIRELESS ACCESS POINT", 32);

	WpaDerivePTK(pAd, temp, temp, pAd->ApCfg.MBSSID[pEntry->func_tb_idx].wdev.bssid, temp,
				pEntry->Addr, pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK, LEN_PTK);
	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("PTK-%x %x %x %x %x %x %x %x \n",
			pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[0],
			pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[1],
			pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[2],
			pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[3],
			pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[4],
			pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[5],
			pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[6],
			pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[7]));


	/* Record the received MIC for check later */
	NdisMoveMemory(rcv_mic, pSTAKey->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
	NdisZeroMemory(pSTAKey->KeyDesc.KeyMic, LEN_KEY_DESC_MIC);
    if (pEntry->WepStatus == Ndis802_11TKIPEnable)
    {
        RT_HMAC_MD5(pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK, LEN_PTK_KCK, (PUCHAR)pSTAKey, MICMsgLen, mic, MD5_DIGEST_SIZE);
    }
    else
    {
        RT_HMAC_SHA1(pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK, LEN_PTK_KCK, (PUCHAR)pSTAKey,  MICMsgLen, digest, SHA1_DIGEST_SIZE);
        NdisMoveMemory(mic, digest, LEN_KEY_DESC_MIC);
    }

    if (!RTMPEqualMemory(rcv_mic, mic, LEN_KEY_DESC_MIC))
    {
        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("MIC Different in STAKey handshake!! \n"));
        return;
    }
    else
        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("MIC VALID in STAKey handshake!! \n"));

	/* Receive init STA's STAKey Message-2, and terminate the handshake */
	/*if (pEntry->bDlsInit && !pSTAKey->KeyDesc.KeyInfo.Request) */
	if (pEntry->bDlsInit && !peerKeyInfo.Request) /*Benson add for big-endian 20081016 --> */
	{
		pEntry->bDlsInit = FALSE;
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("Receive init STA's STAKey Message-2, STAKey handshake finished \n"));
		return;
	}

	/* Receive init STA's STAKey Message-2, and terminate the handshake */
	if (RTMPEqualMemory(&pSTAKey->KeyDesc.KeyData[2], OUI_WPA2_WEP40, 3))
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_WARN, ("Receive a STAKey message which not support currently, just drop it \n"));
		return;
	}
	
    do
    {
    	pDaEntry = MacTableLookup(pAd, DA);
    	if (!pDaEntry)
    		break;

    	if ((pDaEntry->WpaState != AS_PTKINITDONE))
	    {
	        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("Not expect calling STAKey hand shaking here \n"));
	        break;
	    }
    	
		MlmeAllocateMemory(pAd, (PUCHAR *)&pOutBuffer);  /* allocate memory */
        if(pOutBuffer == NULL)
            break;

        MAKE_802_3_HEADER(Header802_3, pDaEntry->Addr, pAd->ApCfg.MBSSID[pDaEntry->apidx].wdev.bssid, EAPOL);

        /* Increment replay counter by 1 */
        ADD_ONE_To_64BIT_VAR(pDaEntry->R_Counter);

		/* Allocate memory for output */
		os_alloc_mem(NULL, (PUCHAR *)&mpool, TX_EAPOL_BUFFER);
		if (mpool == NULL)
	    {
			MlmeFreeMemory(pAd, (PUCHAR)pOutBuffer);
	        MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("!!!%s : no memory!!!\n", __FUNCTION__));
	        return;
	    }

		pOutPacket = (PEAPOL_PACKET)mpool;
		NdisZeroMemory(pOutPacket, TX_EAPOL_BUFFER);

        /* 0. init Packet and Fill header */
        pOutPacket->ProVer = EAPOL_VER;
        pOutPacket->ProType = EAPOLKey;
        pOutPacket->Body_Len[1] = 0x5f;
        
        /* 1. Fill replay counter */
/*        NdisMoveMemory(pDaEntry->R_Counter, pAd->ApCfg.R_Counter, sizeof(pDaEntry->R_Counter)); */
        NdisMoveMemory(pOutPacket->KeyDesc.ReplayCounter, pDaEntry->R_Counter, LEN_KEY_DESC_REPLAY);
        
        /* 2. Fill key version, keyinfo, key len */
        pOutPacket->KeyDesc.KeyInfo.KeyDescVer= GROUP_KEY;
        pOutPacket->KeyDesc.KeyInfo.KeyType	= GROUPKEY;
        pOutPacket->KeyDesc.KeyInfo.Install	= 1;
        pOutPacket->KeyDesc.KeyInfo.KeyAck	= 1;
        pOutPacket->KeyDesc.KeyInfo.KeyMic	= 1;
        pOutPacket->KeyDesc.KeyInfo.Secure	= 1;
        pOutPacket->KeyDesc.KeyInfo.EKD_DL	= 1;
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("STAKey handshake for peer STA %02x:%02x:%02x:%02x:%02x:%02x\n",
			PRINT_MAC(DA)));
        
        if ((pDaEntry->AuthMode == Ndis802_11AuthModeWPA) || (pDaEntry->AuthMode == Ndis802_11AuthModeWPAPSK))
        {
        	pOutPacket->KeyDesc.Type = WPA1_KEY_DESC;

        	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("pDaEntry->AuthMode == Ndis802_11AuthModeWPA/WPAPSK\n"));
        }
        else if ((pDaEntry->AuthMode == Ndis802_11AuthModeWPA2) || (pDaEntry->AuthMode == Ndis802_11AuthModeWPA2PSK))
        {
        	pOutPacket->KeyDesc.Type = WPA2_KEY_DESC;
        	pOutPacket->KeyDesc.KeyDataLen[1] = 0;

        	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("pDaEntry->AuthMode == Ndis802_11AuthModeWPA2/WPA2PSK\n"));
        }

        pOutPacket->KeyDesc.KeyLength[1] = LEN_TKIP_TK;
        pOutPacket->KeyDesc.KeyDataLen[1] = LEN_TKIP_TK;
        pOutPacket->KeyDesc.KeyInfo.KeyDescVer = KEY_DESC_TKIP;
        if (pDaEntry->WepStatus == Ndis802_11AESEnable)
        {
            pOutPacket->KeyDesc.KeyLength[1] = LEN_AES_TK;
            pOutPacket->KeyDesc.KeyDataLen[1] = LEN_AES_TK;
            pOutPacket->KeyDesc.KeyInfo.KeyDescVer = KEY_DESC_AES;
        }

		/* Key Data Encapsulation format, use Ralink OUI to distinguish proprietary and standard. */
    	Key_Data[0] = 0xDD;
		Key_Data[1] = 0x00;		/* Length (This field will be filled later) */
    	Key_Data[2] = 0x00;		/* OUI */
    	Key_Data[3] = 0x0C;		/* OUI */
    	Key_Data[4] = 0x43;		/* OUI */
    	Key_Data[5] = 0x02;		/* Data Type (STAKey Key Data Encryption) */

		/* STAKey Data Encapsulation format */
    	Key_Data[6] = 0x00;		/*Reserved */
		Key_Data[7] = 0x00;		/*Reserved */

		/* STAKey MAC address */
		NdisMoveMemory(&Key_Data[8], pEntry->Addr, MAC_ADDR_LEN);		/* initiator MAC address */

		/* STAKey (Handle the difference between TKIP and AES-CCMP) */
		if (pDaEntry->WepStatus == Ndis802_11AESEnable)
        {
        	Key_Data[1] = 0x1E;	/* 4+2+6+16(OUI+Reserved+STAKey_MAC_Addr+STAKey) */
        	NdisMoveMemory(&Key_Data[14], pEntry->PairwiseKey.Key, LEN_AES_TK);
		}
		else
		{
			Key_Data[1] = 0x2E;	/* 4+2+6+32(OUI+Reserved+STAKey_MAC_Addr+STAKey) */
			NdisMoveMemory(&Key_Data[14], pEntry->PairwiseKey.Key, LEN_TK);
			NdisMoveMemory(&Key_Data[14+LEN_TK], pEntry->PairwiseKey.TxMic, LEN_TKIP_MIC);
			NdisMoveMemory(&Key_Data[14+LEN_TK+LEN_TKIP_MIC], pEntry->PairwiseKey.RxMic, LEN_TKIP_MIC);
		}

		key_length = Key_Data[1];
		pOutPacket->Body_Len[1] = key_length + 0x5f;

		/* This is proprietary DLS protocol, it will be adhered when spec. is finished. */
		NdisZeroMemory(temp, 64);
		NdisMoveMemory(temp, "IEEE802.11 WIRELESS ACCESS POINT", 32);
		WpaDerivePTK(pAd, temp, temp, pAd->ApCfg.MBSSID[pEntry->func_tb_idx].wdev.bssid, temp, DA, pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK, LEN_PTK);
		
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("PTK-0-%x %x %x %x %x %x %x %x \n",
				pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[0],
				pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[1],
				pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[2],
				pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[3],
				pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[4],
				pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[5],
				pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[6],
				pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK[7]));

       	NdisMoveMemory(pOutPacket->KeyDesc.KeyData, Key_Data, key_length);
		NdisZeroMemory(mic, sizeof(mic));

		*(USHORT *)(&pOutPacket->KeyDesc.KeyInfo) = cpu2le16(*(USHORT *)(&pOutPacket->KeyDesc.KeyInfo));

		MakeOutgoingFrame(pOutBuffer,			&FrameLen,
                        pOutPacket->Body_Len[1] + 4,	pOutPacket,
                        END_OF_ARGS);
	    
		/* Calculate MIC */
        if (pDaEntry->WepStatus == Ndis802_11AESEnable)
        {
            RT_HMAC_SHA1(pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK, LEN_PTK_KCK, pOutBuffer, FrameLen, digest, SHA1_DIGEST_SIZE);
            NdisMoveMemory(pOutPacket->KeyDesc.KeyMic, digest, LEN_KEY_DESC_MIC);
	    }
        else
        {
            RT_HMAC_MD5(pAd->ApCfg.MBSSID[pEntry->func_tb_idx].DlsPTK, LEN_PTK_KCK, pOutBuffer, FrameLen, mic, MD5_DIGEST_SIZE);
            NdisMoveMemory(pOutPacket->KeyDesc.KeyMic, mic, LEN_KEY_DESC_MIC);
        }

        RTMPToWirelessSta(pAd, pDaEntry, Header802_3, LENGTH_802_3, (PUCHAR)pOutPacket, pOutPacket->Body_Len[1] + 4, FALSE);

        MlmeFreeMemory(pAd, (PUCHAR)pOutBuffer);
		os_free_mem(NULL, mpool);
    }while(FALSE);
    
    MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("<== RTMPHandleSTAKey: FrameLen=%ld\n", FrameLen));
}
#endif /* QOS_DLS_SUPPORT */

#ifdef HOSTAPD_SUPPORT
/*for sending an event to notify hostapd about michael failure. */
VOID ieee80211_notify_michael_failure(
	IN	PRTMP_ADAPTER    pAd,
	IN	PHEADER_802_11   pHeader,
	IN	UINT            keyix,
	IN	INT              report)
{
	static const char *tag = "MLME-MICHAELMICFAILURE.indication";
/*	struct net_device *dev = pAd->net_dev; */
/*	union iwreq_data wrqu; */
	char buf[128];		/* XXX */


	/* TODO: needed parameters: count, keyid, key type, src address, TSC */
       if(report)/*station reports a mic error to this ap. */
       {
	       snprintf(buf, sizeof(buf), "%s(keyid=%d %scast addr=%s)", tag,
		       keyix, "uni",
		       ether_sprintf(pHeader->Addr2));
       }
       else/*ap itself receives a mic error. */
       {
              snprintf(buf, sizeof(buf), "%s(keyid=%d %scast addr=%s)", tag,
		       keyix, IEEE80211_IS_MULTICAST(pHeader->Addr1) ?  "broad" : "uni",
		       ether_sprintf(pHeader->Addr2));
	 }
	RtmpOSWrielessEventSend(pAd->net_dev, RT_WLAN_EVENT_CUSTOM, -1, NULL, NULL, 0);
/*	NdisZeroMemory(&wrqu, sizeof(wrqu)); */
/*	wrqu.data.length = strlen(buf); */
/*	wireless_send_event(dev, RT_WLAN_EVENT_CUSTOM, &wrqu, buf); */
}


const CHAR* ether_sprintf(const UINT8 *mac)
{
    static char etherbuf[18];
    snprintf(etherbuf,sizeof(etherbuf),"%02x:%02x:%02x:%02x:%02x:%02x", PRINT_MAC(mac));
    return etherbuf;
}
#endif /* HOSTAPD_SUPPORT */


#ifdef APCLI_SUPPORT
#ifdef WPA_SUPPLICANT_SUPPORT 
VOID    ApcliWpaSendEapolStart(
	IN	PRTMP_ADAPTER	pAd,
	IN  PUCHAR          pBssid,
	IN  PMAC_TABLE_ENTRY pMacEntry,
	IN	PAPCLI_STRUCT pApCliEntry)
{
	IEEE8021X_FRAME		Packet;
	UCHAR               Header802_3[14];
	
	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("-----> ApCliWpaSendEapolStart\n"));

	NdisZeroMemory(Header802_3,sizeof(UCHAR)*14);

	MAKE_802_3_HEADER(Header802_3, pBssid, &pApCliEntry->wdev.if_addr[0], EAPOL);
	
	// Zero message 2 body
	NdisZeroMemory(&Packet, sizeof(Packet));
	Packet.Version = EAPOL_VER;
	Packet.Type    = EAPOLStart;
	Packet.Length  = cpu2be16(0);
	
	// Copy frame to Tx ring
	RTMPToWirelessSta((PRTMP_ADAPTER)pAd, pMacEntry,
					 Header802_3, LENGTH_802_3, (PUCHAR)&Packet, 4, TRUE);

	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("<----- WpaSendEapolStart\n"));
}
#endif /* WPA_SUPPLICANT_SUPPORT */ 
VOID	ApCliRTMPReportMicError(
	IN	PRTMP_ADAPTER	pAd, 
	IN UCHAR unicastKey,
	IN	INT			ifIndex)
{
	ULONG	Now;
	PAPCLI_STRUCT pApCliEntry = NULL;
	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, (" ApCliRTMPReportMicError <---\n"));

	pApCliEntry = &pAd->ApCfg.ApCliTab[ifIndex];
	/* Record Last MIC error time and count */
	NdisGetSystemUpTime(&Now);
	if (pAd->ApCfg.ApCliTab[ifIndex].MicErrCnt == 0)
	{
		pAd->ApCfg.ApCliTab[ifIndex].MicErrCnt++;
		pAd->ApCfg.ApCliTab[ifIndex].LastMicErrorTime = Now;
		NdisZeroMemory(pAd->ApCfg.ApCliTab[ifIndex].ReplayCounter, 8);        
	}
	else if (pAd->ApCfg.ApCliTab[ifIndex].MicErrCnt == 1)
	{
		if ((pAd->ApCfg.ApCliTab[ifIndex].LastMicErrorTime + (60 * OS_HZ)) < Now)
		{
			/* Update Last MIC error time, this did not violate two MIC errors within 60 seconds */
			pAd->ApCfg.ApCliTab[ifIndex].LastMicErrorTime = Now; 		
		}
		else
		{

			/* RTMPSendWirelessEvent(pAd, IW_COUNTER_MEASURES_EVENT_FLAG, pAd->MacTab.Content[BSSID_WCID].Addr, BSS0, 0); */

			pAd->ApCfg.ApCliTab[ifIndex].LastMicErrorTime = Now; 		
			/* Violate MIC error counts, MIC countermeasures kicks in */
			pAd->ApCfg.ApCliTab[ifIndex].MicErrCnt++;
			/*
			 We shall block all reception
			 We shall clean all Tx ring and disassoicate from AP after next EAPOL frame
			 
			 No necessary to clean all Tx ring, on RTMPHardTransmit will stop sending non-802.1X EAPOL packets
			 if pAd->StaCfg.MicErrCnt greater than 2.
			*/
		}
	}
	else
	{
		/* MIC error count >= 2 */
		/* This should not happen */
		;
	}
	MlmeEnqueue(pAd, APCLI_CTRL_STATE_MACHINE, APCLI_MIC_FAILURE_REPORT_FRAME, 1, &unicastKey, ifIndex);

	if (pAd->ApCfg.ApCliTab[ifIndex].MicErrCnt == 2)
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, (" MIC Error count = 2 Trigger Block timer....\n"));
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, (" pAd->ApCfg.ApCliTab[%d].LastMicErrorTime = %ld\n",ifIndex,
			pAd->ApCfg.ApCliTab[ifIndex].LastMicErrorTime));
		
		RTMPSetTimer(&pApCliEntry->MlmeAux.WpaDisassocAndBlockAssocTimer, 100);
	}
	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("ApCliRTMPReportMicError --->\n"));

}

VOID ApCliWpaDisassocApAndBlockAssoc(
    IN PVOID SystemSpecific1, 
    IN PVOID FunctionContext, 
    IN PVOID SystemSpecific2, 
    IN PVOID SystemSpecific3) 
{

	RTMP_ADAPTER                *pAd = (PRTMP_ADAPTER)FunctionContext;
	MLME_DISASSOC_REQ_STRUCT    DisassocReq;
	
	PAPCLI_STRUCT pApCliEntry;

	pAd->ApCfg.ApCliTab[0].bBlockAssoc = TRUE;
	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("(%s) disassociate with current AP after sending second continuous EAPOL frame.\n", __FUNCTION__));


	pApCliEntry = &pAd->ApCfg.ApCliTab[0];

	DisassocParmFill(pAd, &DisassocReq, pApCliEntry->MlmeAux.Bssid, REASON_MIC_FAILURE);
	MlmeEnqueue(pAd, APCLI_ASSOC_STATE_MACHINE, APCLI_MT2_MLME_DISASSOC_REQ,
		sizeof(MLME_DISASSOC_REQ_STRUCT), &DisassocReq, 0);

	pApCliEntry->MicErrCnt = 0;
}

#endif/*APCLI_SUPPORT*/

INT	Set_PtkRekey_Proc(
	IN  PRTMP_ADAPTER pAd,
	IN  RTMP_STRING *arg)
{
	UCHAR wcid = 0;
	MAC_TABLE_ENTRY *pEntry = NULL;
	STA_TR_ENTRY *tr_entry = NULL;

	wcid = (UCHAR)simple_strtol(arg, 0, 10);
	if (!VALID_UCAST_ENTRY_WCID(wcid))
		return FALSE;

	pEntry = &pAd->MacTab.Content[wcid];

	if (!pEntry || !IS_ENTRY_CLIENT(pEntry)) {
		MTWF_LOG(DBG_CAT_CFG, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("%s: pEntry is null\n",
			 __func__));
		return FALSE;
	}

	tr_entry = &pAd->MacTab.tr_entry[pEntry->wcid];
	if (!tr_entry) {
		MTWF_LOG(DBG_CAT_CFG, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("%s: tr_entry is null\n",
			 __func__));
		return FALSE;
	}

	if (((pEntry->AuthMode == Ndis802_11AuthModeWPAPSK) || (pEntry->AuthMode == Ndis802_11AuthModeWPA2PSK)
#ifdef DOT11_SAE_SUPPORT
	|| (pEntry->AuthMode == Ndis802_11AuthModeWPA3PSK)
#endif
#ifdef CONFIG_OWE_SUPPORT
	|| (pEntry->AuthMode == Ndis802_11AuthModeOWE)
#endif
	|| ((pEntry->AuthMode == Ndis802_11AuthModeWPA2) && (pEntry->PMKID_CacheIdx != ENTRY_NOT_FOUND))))
	{
		pEntry->PrivacyFilter = Ndis802_11PrivFilter8021xWEP;
		pEntry->WpaState = AS_INITPSK;
		tr_entry->PortSecured = WPA_802_1X_PORT_NOT_SECURED;
		/*NdisZeroMemory(pEntry->R_Counter, sizeof(pEntry->R_Counter));*/
		pEntry->ReTryCounter = PEER_MSG1_RETRY_TIMER_CTR;

		WPAStart4WayHS(pAd, pEntry, PEER_MSG1_RETRY_EXEC_INTV);
	}

	MTWF_LOG(DBG_CAT_CFG, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("%s::(WCID=%d)\n",
			 __func__, wcid));

	return TRUE;
}
