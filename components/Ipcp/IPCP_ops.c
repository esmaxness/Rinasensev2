#include <stdio.h>
#include <string.h>

#include "common/list.h"
#include "common/rina_ids.h"
#include "common/rina_name.h"
#include "common/rina_common_port.h"
#include "portability/port.h"

#include "BufferManagement.h"
#include "configRINA.h"

#include "IPCP_api.h"
#include "IPCP_manager.h"
#include "efcp.h"
#include "rmt.h"
#include "du.h"
#include "flowAllocator.h"
#include "ribd.h"
#include "ribd_api.h"

#include "rina_api_flows.h"

extern struct ipcpInstanceData_t *pxIpcpData;

struct cepIdsEntry_t
{
        cepId_t xCepId;
        RsListItem_t CepIdListItem;
};

struct normalFlow_t
{
        portId_t xPortId;
        cepId_t xActive;
        RsList_t xCepIdsList;
        eNormalFlowState_t eState;
        flowAllocateHandle_t *pxFlowHandle;
        RsListItem_t xFlowListItem;
};

struct ipcpFactoryData_t
{
        RsList_t xInstancesNormalList;
};

static struct ipcpFactoryData_t xFactoryNormalData;

static bool_t pvNormalAssignToDif(struct ipcpInstanceData_t *pxData, name_t *pxDifName);

static bool_t xNormalInit(struct ipcpFactoryData_t *pxData)
{
        // memset(&xNormalData, 0, sizeof(xNormalData));
        vRsListInit(&pxData->xInstancesNormalList);
        return true;
}

static struct ipcpInstance_t *prvNormalIPCPFindInstance(struct ipcpFactoryData_t *pxFactoryData,
                                                        ipcpInstanceType_t xType);

static struct ipcpInstance_t *prvNormalIPCPFindInstance(struct ipcpFactoryData_t *pxFactoryData,
                                                        ipcpInstanceType_t xType)
{
        struct ipcpInstance_t *pxInstance;
        RsListItem_t *pxListItem;

        pxInstance = pvRsMemAlloc(sizeof(*pxInstance));

        /* Find a way to iterate in the list and compare the addesss*/

        pxListItem = pxRsListGetFirst(&pxFactoryData->xInstancesNormalList);

        while (pxListItem != NULL)
        {
                pxInstance = (struct ipcpInstance_t *)pxRsListGetItemOwner(pxListItem);

                if (pxInstance)
                {

                        if (pxInstance->xType == xType)
                        {
                                // LOGI(TAG_IPCPMANAGER, "Instance founded %p, Type: %d", pxInstance, pxInstance->xType);
                                return pxInstance;
                        }
                }
                else
                {
                        return NULL;
                }

                pxListItem = pxRsListGetNext(pxListItem);
        }

        return NULL;
}

static struct normalFlow_t *prvNormalFindFlow(struct ipcpInstanceData_t *pxData,
                                              portId_t xPortId)
{
        struct normalFlow_t *pxFlow;
        RsListItem_t *pxListItem;

        pxFlow = pvRsMemAlloc(sizeof(*pxFlow));

        /* Find a way to iterate in the list and compare the addesss*/
        pxListItem = pxRsListGetFirst(&pxData->xFlowsList);

        while (pxListItem != NULL)
        {
                pxFlow = (struct normalFlow_t *)pxRsListGetItemOwner(pxListItem);

                if (!pxFlow)
                        return false;

                if (pxFlow && pxFlow->xPortId == xPortId)
                {
                        return pxFlow;
                }

                pxListItem = pxRsListGetNext(pxListItem);
        }

        return NULL;
}

bool_t xNormalDuWrite(struct ipcpInstanceData_t *pxData,
                      portId_t xAppPortId,
                      NetworkBufferDescriptor_t *pxNetworkBuffer)
{
        LOGD(TAG_IPCPNORMAL, "Writing Data into the IPCP Normal");
        struct du_t *pxDu;
        struct normalFlow_t *pxFlow;

        pxDu = pvRsMemAlloc(sizeof(*pxDu));

        pxDu->pxNetworkBuffer = pxNetworkBuffer;

        pxFlow = prvNormalFindFlow(pxData, xAppPortId);
        if (!pxFlow || pxFlow->eState != ePORT_STATE_ALLOCATED)
        {

                LOGE(TAG_IPCPNORMAL, "Write: There is no flow bound to this port ID: %u",
                     xAppPortId);
                xDuDestroy(pxDu);
                return false;
        }

        if (!xEfcpContainerWrite(pxData->pxEfcpc, pxFlow->xActive, pxDu))
        {
                LOGE(TAG_IPCPNORMAL, "Could not send sdu to EFCP Container");
                return false;
        }

        return true;
}

/*static BaseType_t xNormalDeallocate(struct ipcpInstanceData * pxData)
{
        struct normalFlow_t * pxFlow, * pxNextFlow;

        list_for_each_entry_safe(flow, next, &(data->flows), list) {
                if (remove_all_cepid(data, flow))
                        LOG_ERR("Some efcp structures could not be destroyed"
                                "in flow %d", flow->port_id);

                list_del(&flow->list);
                rkfree(flow);
        }

        return pdTRUE;
}*/

bool_t xNormalFlowPrebind(struct ipcpInstanceData_t *pxData,
                          flowAllocateHandle_t *pxFlowAllocateHandle)
{

        struct normalFlow_t *pxFlow;

        LOGI(TAG_IPCPNORMAL, "Binding the flow with port ID: %u", pxFlowAllocateHandle->xPortId);

        if (!pxData)
        {
                LOGE(TAG_IPCPNORMAL, "Wrong input parameters...");
                return false;
        }

        pxFlow = pvRsMemAlloc(sizeof(*pxFlow));
        if (!pxFlow)
        {
                LOGE(TAG_IPCPNORMAL, "Could not create a flow in normal-ipcp to pre-bind");
                return false;
        }

        pxFlow->xPortId = pxFlowAllocateHandle->xPortId;
        pxFlow->eState = ePORT_STATE_PENDING;

        LOGD(TAG_IPCPNORMAL, "Flow: %p portID: %d portState: %d", pxFlow, pxFlow->xPortId, pxFlow->eState);
        vRsListInitItem(&(pxFlow->xFlowListItem), pxFlow);
        vRsListInsert(&(pxData->xFlowsList), &(pxFlow->xFlowListItem));

        return true;
}

cepId_t xNormalConnectionCreateRequest(struct efcpContainer_t *pxEfcpc,
                                       portId_t xAppPortId,
                                       address_t xSource,
                                       address_t xDest,
                                       qosId_t xQosId,
                                       dtpConfig_t *pxDtpCfg,
                                       struct dtcpConfig_t *pxDtcpCfg)
{
        cepId_t xCepId;
        struct normalFlow_t *pxFlow;
        struct cepIdsEntry_t *pxCepEntry;
        struct ipcpInstance_t *pxIpcp;

        xCepId = xEfcpConnectionCreate(pxEfcpc, xSource, xDest,
                                       xAppPortId, xQosId,
                                       CEP_ID_WRONG, CEP_ID_WRONG,
                                       pxDtpCfg, pxDtcpCfg);

        if (!is_cep_id_ok(xCepId))
        {
                LOGE(TAG_IPCPNORMAL, "Failed EFCP connection creation");
                return CEP_ID_WRONG;
        }

        /*        pxCepEntry = pvPortMalloc(sizeof(*pxCepEntry)); // error
                if (!pxCepEntry)
                {
                        LOGE(TAG_IPCPNORMAL, "Could not create a cep_id entry, bailing out");
                        xEfcpConnectionDestroy(pxData->pxEfcpc, xCepId);
                        return cep_id_bad();
                }*/

        // vListInitialise(&pxCepEntry->CepIdListItem);
        // pxCepEntry->xCepId = xCepId;

        /*/ipcp = kipcm_find_ipcp(default_kipcm, data->id);
        if (!ipcp) {
                LOGE(TAG_IPCPNORMALNORMAL,"KIPCM could not retrieve this IPCP");
                xEfcpConnectionDestroy(pxData->efcpc, cep_id);
                return cep_id_bad();
        }*/

        // configASSERT(xUserIpcp->xOps);
        // configASSERT(xUserIpcp->xOps->flow_binding_ipcp);
        // spin_lock_bh(&data->lock);
        // pxFlow = prvNormalFindFlow(pxData, xPortId);

        /*
                if (!pxFlow) {
                        //spin_unlock_bh(&data->lock);
                        LOGE(TAG_IPCPNORMAL,"Could not retrieve normal flow to create connection");
                        xEfcpConnectionDestroy(pxData->efcpc, xCepId);
                        return cep_id_bad();
                }

                if (user_ipcp->ops->flow_binding_ipcp(user_ipcp->data,
                                                      port_id,
                                                      ipcp)) {
                        spin_unlock_bh(&data->lock);
                        LOGE(TAG_IPCPNORMAL,"Could not bind flow with user_ipcp");
                        efcp_connection_destroy(data->efcpc, cep_id);
                        return cep_id_bad();
                }

                list_add(&cep_entry->list, &flow->cep_ids_list);*/

        // pxFlow->xActive = xCepId;
        // pxFlow->eState = ePORTSTATEPENDING;

        // spin_unlock_bh(&data->lock);

        return xCepId;
}

/**
 * @brief Flow binding the N-1 Instance and the Normal IPCP by the
 * portId (N-1 port Id).
 *
 * @param pxUserData Normal IPCP in this case
 * @param xPid The PortId of the N-1 DIF
 * @param pxN1Ipcp Ipcp Instance N-1 DIF
 * @return BaseType_t
 */
bool_t xNormalFlowBinding(struct ipcpInstanceData_t *pxUserData,
                          portId_t xPid,
                          struct ipcpInstance_t *pxN1Ipcp)
{
        bool_t xresult;

        xresult = xRmtN1PortBind(pxUserData->pxRmt, xPid, pxN1Ipcp);

        return xresult;
}

static bool_t pvNormalAssignToDif(struct ipcpInstanceData_t *pxData, name_t *pxDifName)
{
        efcpConfig_t *pxEfcpConfig;
        // struct secman_config * sm_config;
        // rmtConfig_t *pxRmtConfig;

        if (!pxDifName)
        {
                LOGE(TAG_IPCPNORMAL, "Failed creation of dif name");

                return false;
        }

        if (!xRstringDup(pxDifName->pcProcessName, &pxData->pxDifName->pcProcessName) ||
            !xRstringDup(pxDifName->pcProcessInstance, &pxData->pxDifName->pcProcessInstance) ||
            !xRstringDup(pxDifName->pcEntityName, &pxData->pxDifName->pcEntityName) ||
            !xRstringDup(pxDifName->pcEntityInstance, &pxData->pxDifName->pcEntityInstance))
        {
                LOGE(TAG_IPCPNORMAL, "Name was not created properly");
        }

        /*Reading from the RINACONFIG.h*/
        pxData->xAddress = LOCAL_ADDRESS;

        /* FUTURE IMPLEMENTATIONS

        **** SHOULD READ CONFIGS FROM FILE RINACONFIG.H
        pxEfcpConfig =  pxConfig->pxEfcpconfig;
        pxConfig->pxEfcpconfig = 0;

        if (! pxEfcpConfig)
        {
                LOGE(TAG_IPCPNORMAL, "No EFCP configuration in the dif_info");
                return pdFALSE;
        }

        if (!pxEfcpConfig->pxDtCons)
        {
                LOGE(TAG_IPCPNORMAL, "Configuration constants for the DIF are bogus...");
                efcp_config_free(efcp_config);
                return pdFALSE;
        }

        efcp_container_config_set(pxData->pxEfcpc, pxEfcpConfig);


        pxRmtConfig = pxConfig->pxRmtConfig;
        pxConfig->pxRmtConfig = 0;

        if (!pxRmtConfig)
        {
                LOGE(TAG_IPCPNORMAL, "No RMT configuration in the dif_info");
                return pdFALSE;
        }*/

        if (!xRmtAddressAdd(pxData->pxRmt, pxData->xAddress))
        {
                LOGE(TAG_IPCPNORMAL, "Could not set local Address to RMT");
                return false;
        }
        /*
                if (rmt_config_set(data->rmt, rmt_config))
                {
                        LOGE(TAG_IPCPNORMAL, "Could not set RMT conf");
                        return pdFALSE;
                }

                sm_config = config->secman_config;
                config->secman_config = 0;
                if (!sm_config)
                {
                        LOG_INFO("No SDU protection config specified, using default");
                        sm_config = secman_config_create();
                        sm_config->default_profile = auth_sdup_profile_create();
                }
                if (sdup_config_set(data->sdup, sm_config))
                {
                        LOGE(TAG_IPCPNORMAL, "Could not set SDUP conf");
                        return -1;
                }
                if (sdup_dt_cons_set(data->sdup, dt_cons_dup(efcp_config->dt_cons)))
                {
                        LOGE(TAG_IPCPNORMAL, "Could not set dt_cons in SDUP");
                        return -1;
                }*/

        return true;
}

bool_t xNormalDuEnqueue(struct ipcpInstanceData_t *pxData,
                        portId_t xN1PortId,
                        struct du_t *pxDu)
{

        if (!xRmtReceive(pxData, pxDu, xN1PortId))
        {
                LOGE(TAG_IPCPNORMAL, "Could not enqueue SDU into the RMT");
                return false;
        }

        return true;
}

/**
 * @brief Write the DU into the IPCP instance
 *
 * @param pxData Ipcp Instance Data (Normal Instance)
 * @param xPortId Port Id N-1 of the flow. Enrollment task know the Port Id when it request a flow to the
 * N-1 DIF.
 * @param pxDu Data Unit to be write into the IPCP instance.
 * @return BaseType_t
 */
bool_t xNormalMgmtDuWrite(struct rmt_t *pxRmt, portId_t xPortId, struct du_t *pxDu)
{
        ssize_t sbytes;

        LOGI(TAG_IPCPNORMAL, "Passing SDU to be written to N-1 port %d ", xPortId);

        if (!pxRmt)
        {
                LOGE(TAG_IPCPNORMAL, "No RMT passed");
                return false;
        }

        if (!pxDu)
        {
                LOGE(TAG_IPCPNORMAL, "No data passed, bailing out");
                return false;
        }

        // pxDu->pxCfg = pxData->pxEfcpc->pxConfig;
        /* SET BUT NOT USED: sbytes = xDuLen(pxDu); */

        if (!xDuEncap(pxDu, PDU_TYPE_MGMT))
        {
                LOGE(TAG_IPCPNORMAL, "No data passed, bailing out");
                xDuDestroy(pxDu);
                return false;
        }

        /*Fill the PCI*/
        pxDu->pxPci->ucVersion = 0X01;
        pxDu->pxPci->connectionId_t.xDestination = 0;
        pxDu->pxPci->connectionId_t.xQosId = 1;
        pxDu->pxPci->connectionId_t.xSource = 0;
        pxDu->pxPci->xDestination = 0;
        pxDu->pxPci->xFlags = 0;
        pxDu->pxPci->xType = PDU_TYPE_MGMT;
        pxDu->pxPci->xSequenceNumber = 0;
        pxDu->pxPci->xPduLen = pxDu->pxNetworkBuffer->xDataLength;
        pxDu->pxPci->xSource = LOCAL_ADDRESS;

        // vPciPrint(pxDu->pxPci);

        // pxRmt = pxIpcpGetRmt();

        if (xPortId)
        {
                if (!xRmtSendPortId(pxRmt, xPortId, pxDu))
                {
                        LOGE(TAG_IPCPNORMAL, "Could not sent to RMT");
                        return false;
                }
        }
        else
        {
                LOGE(TAG_IPCPNORMAL, "Could not sent to RMT: no portID");
                xDuDestroy(pxDu);
                return false;
        }

        return true;
}

bool_t xNormalMgmtDuPost(struct ipcpInstanceData_t *pxData, portId_t xPortId, struct du_t *pxDu)
{

        if (!is_port_id_ok(xPortId))
        {
                LOGE(TAG_IPCPNORMAL, "Wrong port id");
                xDuDestroy(pxDu);
                return false;
        }
        /*if (!IsDuOk(pxDu)) {
                LOGE(TAG_IPCPNORMAL,"Bogus management SDU");
                xDuDestroy(pxDu);
                return pdFALSE;
        }*/

        /*Send to the RIB Daemon*/
        if (!xRibdProcessLayerManagementPDU(pxData, xPortId, pxDu))
        {
                LOGI(TAG_IPCPNORMAL, "Was not possible to process el Management PDU");
                return false;
        }

        return true;
}

/* Called from the IPCP Task to the NormalIPCP register as APP into the SHIM DIF
 * depending on the Type of ShimDIF.*/
bool_t xNormalRegistering(struct ipcpInstance_t *pxShimInstance,
                          name_t *pxDifName,
                          name_t *pxName)
{

        if (pxShimInstance->pxOps->applicationRegister == NULL)
        {
                LOGI(TAG_IPCPNORMAL, "There is not Application Register API");
        }
        if (pxShimInstance->pxOps->applicationRegister(pxShimInstance->pxData, pxName, pxDifName))
        {
                LOGI(TAG_IPCPNORMAL, "Normal Instance Registered into the Shim");
                return true;
        }

        return false;
}

/* Normal IPCP request a Flow Allocation to the Shim */

#if 0
bool_t xNormalFlowAllocationRequest(ipcpInstance_t *pxInstanceFrom, ipcpInstance_t *pxInstanceTo, portId_t xShimPortId)
{
        /*This should be proposed by the Flow Allocator?*/
        name_t *destinationInfo = pvRsMemAlloc(sizeof(*destinationInfo));
        destinationInfo->pcProcessName = REMOTE_ADDRESS_AP_NAME;
        destinationInfo->pcEntityName = "";
        destinationInfo->pcProcessInstance = REMOTE_ADDRESS_AP_INSTANCE;
        destinationInfo->pcEntityInstance = "";

        if (pxInstanceTo->pxOps->flowAllocateRequest == NULL)
        {
                LOGI(TAG_IPCPNORMAL, "There is not Flow Allocate Request API");
        }
        if (pxInstanceTo->pxOps->flowAllocateRequest(xShimPortId,
                                                     pxInstanceFrom,
                                                     pxInstanceTo->pxData->pxName,
                                                     destinationInfo,
                                                     pxInstanceTo->pxData))
        {
                LOGI(TAG_IPCPNORMAL, "Flow Request Sended");
                return true;
        }

        return false;
}

bool_t xNormalAppFlowAllocationRequestHandle(ipcpInstance_t)



    /* Call to xNormalFlowBind */

    /* Call to xNormalConnectionCreate */
    xNormalFlowPrebind(pxNormalInstance->pxData, xPortId);

address_t xSource = 10;
address_t xDest = 3;
qosId_t xQosId = 1;
dtpConfig_t *pxDtpCfg = pvPortMalloc(sizeof(*pxDtpCfg));
struct dtcpConfig_t *pxDtcpCfg = pvPortMalloc(sizeof(*pxDtcpCfg));

xNormalConnectionCreateRequest(pxNormalInstance->pxData, xPortId,
                               xSource, xDest, xQosId, pxDtpCfg,
                               pxDtcpCfg);

#endif

bool_t xNormalUpdateFlowStatus(portId_t xPortId, eNormalFlowState_t eNewFlowstate)
{
        struct normalFlow_t *pxFlow = NULL;

        pxFlow = prvNormalFindFlow(pxIpcpData, xPortId);
        if (!pxFlow)
        {
                LOGE(TAG_IPCPNORMAL, "Flow not found");
                return false;
        }
        pxFlow->eState = eNewFlowstate;
        LOGI(TAG_IPCPNORMAL, "Flow state updated");

        return true;
}

bool_t xNormalIsFlowAllocated(portId_t xPortId)
{
        struct normalFlow_t *pxFlow = NULL;

        pxFlow = prvNormalFindFlow(pxIpcpData, xPortId);
        if (!pxFlow)
        {
                LOGE(TAG_IPCPNORMAL, "Flow not found");
                return false;
        }
        if (pxFlow->eState == ePORT_STATE_ALLOCATED)
        {
                LOGI(TAG_IPCPNORMAL, "Flow status: Allocated");
                return true;
        }

        return false;
}

bool_t xNormalUpdateCepIdFlow(portId_t xPortId, cepId_t xCepId)
{
        struct normalFlow_t *pxFlow = NULL;

        pxFlow = prvNormalFindFlow(pxIpcpData, xPortId);
        if (!pxFlow)
        {
                LOGE(TAG_IPCPNORMAL, "Flow not found");
                return false;
        }
        pxFlow->xActive = xCepId;

        return true;
}

bool_t xNormalConnectionModify(cepId_t xCepId,
                               address_t xSrc,
                               address_t xDst)
{
        struct efcpContainer_t *pxEfcpContainer;
        pxEfcpContainer = pxIPCPGetEfcpc();
        if (!pxEfcpContainer)
        {
                LOGE(TAG_EFCP, "No EFCP Container available");
                return false;
        }
        if (!xEfcpConnectionModify(pxEfcpContainer, xCepId,
                                   xSrc,
                                   xDst))
                return false;
        return true;
}

bool_t xNormalConnectionUpdate(portId_t xAppPortId, cepId_t xSrcCepId, cepId_t xDstCepId)
{
        struct efcpContainer_t *pxEfcpContainer;
        struct normalFlow_t *pxFlow = NULL;

        pxEfcpContainer = pxIPCPGetEfcpc();

        if (!xEfcpConnectionUpdate(pxEfcpContainer,
                                   xSrcCepId,
                                   xDstCepId))
                return false;

        pxFlow = prvNormalFindFlow(pxIpcpData, xAppPortId);
        if (!pxFlow)
        {
                LOGE(TAG_IPCPNORMAL, "Flow not found");
                return false;
        }
        pxFlow->eState = ePORT_STATE_ALLOCATED;
        LOGI(TAG_IPCPNORMAL, "Flow state updated");

        return true;
}

static bool_t prvRemoveCepIdFromFlow(struct normalFlow_t *pxFlow,
                                     cepId_t xCepId)
{
#if 0
        

        LOGI(TAG_IPCPNORMAL, "Finding a Flow in the normal IPCP list");

        struct normalFlow_t *pxFlow;

        // shimFlow_t *pxFlowNext;

        ListItem_t *pxListItem;
        ListItem_t const *pxListEnd;

        if (listLIST_IS_EMPTY(&(pxFlow->xCepIdsList)) == pdTRUE)
        {
                LOGI(TAG_IPCPNORMAL, "Flow CepIds list is empty");
                return NULL;
        }

        pxFlow = pvPortMalloc(sizeof(*pxFlow));

        /* Find a way to iterate in the list and compare the addesss*/
        //pxListEnd = listGET_END_MARKER(&(pxIpcpData->xFlowsList));
        //pxListItem = listGET_HEAD_ENTRY(&(pxIpcpData->xFlowsList));

        while (pxListItem != pxListEnd)
        {

                pxFlow = (struct normalFlow_t *)listGET_LIST_ITEM_OWNER(pxListItem);

                if (pxFlow == NULL)
                        return pdFALSE;

                if (!pxFlow)
                        return pdFALSE;

                if (pxFlow && pxFlow->xActive == xCepId)
                {

                        // LOGI(TAG_IPCPNORMAL, "Flow founded %p, portID: %d, portState:%d", pxFlow, pxFlow->xPortId, pxFlow->eState);
                        return pxFlow;
                }

                pxListItem = listGET_NEXT(pxListItem);
        }

        LOGI(TAG_IPCPNORMAL, "Flow not founded");
        return NULL;

        struct cep_ids_entry *pos, *next;

        list_for_each_entry_safe(pos, next, &(flow->cep_ids_list), list)
        {
                if (pos->cep_id == id)
                {
                        list_del(&pos->list);
                        rkfree(pos);
                        return 0;
                }
        }
        return -1;
#endif
        return false;
}

static struct normalFlow_t *prvFindFlowCepid(cepId_t xCepId)
{

        LOGI(TAG_IPCPNORMAL, "Finding a Flow in the normal IPCP list");

        struct normalFlow_t *pxFlow;

        // shimFlow_t *pxFlowNext;

        RsListItem_t *pxListItem;

        if (unRsListLength(&(pxIpcpData->xFlowsList)) == 0)
        {
                LOGI(TAG_IPCPNORMAL, "Flow list is empty");
                return NULL;
        }

        pxFlow = pvRsMemAlloc(sizeof(*pxFlow));

        /* Find a way to iterate in the list and compare the addesss*/
        pxListItem = pxRsListGetFirst(&(pxIpcpData->xFlowsList));

        while (pxListItem != NULL)
        {
                pxFlow = (struct normalFlow_t *)pxRsListGetItemOwner(pxListItem);

                if (!pxFlow)
                        return false;

                if (pxFlow && pxFlow->xActive == xCepId)
                {

                        // LOGI(TAG_IPCPNORMAL, "Flow founded %p, portID: %d, portState:%d", pxFlow, pxFlow->xPortId, pxFlow->eState);
                        return pxFlow;
                }

                pxListItem = pxRsListGetNext(pxListItem);
        }

        LOGI(TAG_IPCPNORMAL, "Flow not founded");
        return NULL;
}

bool_t xNormalConnectionDestroy(cepId_t xSrcCepId)
{
        struct normalFlow_t *pxFlow;

        if (xEfcpConnectionDestroy(pxIpcpData->pxEfcpc, xSrcCepId))
                LOGE(TAG_EFCP, "Could not destroy EFCP instance: %d", xSrcCepId);

        /* FIXME: The condition below is always TRUE.
        // CRITICAL
        if (!(&pxIpcpData->xFlowsList))
        {
                // CRITICAL
                LOGE(TAG_EFCP, "Could not destroy EFCP instance: %d", xSrcCepId);
                return false;
        }
        */

        pxFlow = prvFindFlowCepid(xSrcCepId);
        if (!pxFlow)
        {
                // CRITICAL
                LOGE(TAG_IPCPNORMAL, "Could not retrieve flow by cep_id :%d", xSrcCepId);
                return false;
        }
        /*if (remove_cep_id_from_flow(flow, src_cep_id))
                LOG_ERR("Could not remove cep_id: %d", src_cep_id);

        if (list_empty(&flow->cep_ids_list))
        {
                list_del(&flow->list);
                rkfree(flow);
        }*/
        // CRITICAL

        return true;
}

static struct ipcpInstanceOps_t xNormalInstanceOps = {
    .flowAllocateRequest = NULL,           // ok
    .flowAllocateResponse = NULL,          // ok
    .flowDeallocate = NULL,                // ok
    .flowPrebind = xNormalFlowPrebind,     // ok
    .flowBindingIpcp = xNormalFlowBinding, // ok
    .flowUnbindingIpcp = NULL,             // ok
    .flowUnbindingUserIpcp = NULL,         // ok
    .nm1FlowStateChange = NULL,            // ok

    .applicationRegister = NULL,   // ok
    .applicationUnregister = NULL, // ok

    .assignToDif = NULL,     // ok
    .updateDifConfig = NULL, // ok

    .connectionCreate = xNormalConnectionCreateRequest, // ok
    .connectionUpdate = NULL,                           // ok
    .connectionDestroy = NULL,                          // ok
    .connectionCreateArrived = NULL,                    // ok
    .connectionModify = NULL,                           // ok

    .duEnqueue = xNormalDuEnqueue, // ok
    .duWrite = NULL,   // ok

    .mgmtDuWrite = NULL, // ok
    .mgmtDuPost = NULL,  // ok

    .pffAdd = NULL,    // ok
    .pffRemove = NULL, // ok
    //.pff_dump                  = NULL,
    //.pff_flush                 = NULL,
    //.pff_modify		   		   = NULL,

    //.query_rib		  		   = NULL,

    .ipcpName = NULL, // ok
    .difName = NULL,  // ok
    //.ipcp_id		  		   = NULL,

    //.set_policy_set_param      = NULL,
    //.select_policy_set         = NULL,
    //.update_crypto_state	   = NULL,
    //.address_change            = NULL,
    //.dif_name		   		   = NULL,
    .maxSduSize = NULL};

/** @brief called by the IPCP manager to create the IPCP instance and its modules */
struct ipcpInstance_t *pxIpcpCreate(ipcProcessId_t xIpcpId)
{

        struct ipcpInstance_t *pxIpcpInstance;
        struct ipcpInstanceData_t *pxInstData;

        /*DIF Name and IPCPName*/
        name_t *pxDifName,
            *pxIPCPName;

        /*** IPCP Modules ***/
        /* RMT module */
        struct rmt_t *pxRmt;

        /* EFCP Container */
        struct efcpContainer_t *pxEfcpContainer;

        pxIpcpData = pvRsMemAlloc(sizeof(*pxIpcpData));
        pxIPCPName = pvRsMemAlloc(sizeof(*pxIPCPName));
        pxDifName = pvRsMemAlloc(sizeof(*pxDifName));
        pxIpcpInstance = pvRsMemAlloc(sizeof(*pxIpcpInstance));

        if (pxIpcpData == NULL || pxIPCPName == NULL || pxDifName == NULL || pxIpcpInstance == NULL)
                goto fail;

        pxIPCPName->pcEntityInstance = NORMAL_ENTITY_INSTANCE;
        pxIPCPName->pcEntityName = NORMAL_ENTITY_NAME;
        pxIPCPName->pcProcessInstance = NORMAL_PROCESS_INSTANCE;
        pxIPCPName->pcProcessName = NORMAL_PROCESS_NAME;

        pxDifName->pcProcessName = NORMAL_DIF_NAME;
        pxDifName->pcProcessInstance = "";
        pxDifName->pcEntityInstance = "";
        pxDifName->pcEntityName = "";

        /* Create EFPC Container */
        pxEfcpContainer = pxEfcpContainerCreate();
        if (!pxEfcpContainer)
        {
                LOGE(TAG_IPCPMANAGER, "Failed creation of EFCP container");
                goto fail;
        }

        /* Create RMT*/
        pxRmt = pxRmtCreate(pxEfcpContainer);
        if (!pxRmt)
        {
                LOGE(TAG_IPCPMANAGER, "Failed creation of RMT instance");
                goto fail;
        }

        pxEfcpContainer->pxRmt = pxRmt;

        pxIpcpData->pxDifName = pxDifName;
        pxIpcpData->pxName = pxIPCPName;
        pxIpcpData->pxEfcpc = pxEfcpContainer;
        pxIpcpData->pxRmt = pxRmt;
        pxIpcpData->xAddress = LOCAL_ADDRESS;

        // pxIpcpData->pxFa = pxFlowAllocatorInit();

        // pxIpcpData->pxIpcManager = pxIpcManager;
        /*Initialialise flows list*/
        vRsListInit(&(pxIpcpData->xFlowsList));

        pxIpcpInstance->xId = xIpcpId;
        pxIpcpInstance->xType = eNormal;
        pxIpcpInstance->pxData = pxIpcpData;
        pxIpcpInstance->pxOps = &xNormalInstanceOps;

        LOGI(TAG_IPCPNORMAL, "Instance Created: %p, IPCP id:%d, Type: %d", pxIpcpInstance, pxIpcpInstance->xId, pxIpcpInstance->xType);

        return pxIpcpInstance;
        ;

fail:
        /* FIXME: is this cleanup necessary since failing here this means
         * nothing will work. */
        LOGE(TAG_IPCPMANAGER, "IPCP not created properly");
        if (pxIpcpData != NULL)
                vRsMemFree(pxIpcpData);
        if (pxIPCPName != NULL)
                vRsMemFree(pxIPCPName);
        if (pxDifName != NULL)
                vRsMemFree(pxDifName);
        if (pxIpcpInstance != NULL)
                vRsMemFree(pxIpcpInstance);

        return NULL;
}