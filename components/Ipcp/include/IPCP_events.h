#ifndef _COMPONENTS_IPCP_EVENTS_H
#define _COMPONENTS_IPCP_EVENTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum RINA_EVENTS
    {
        eNoEvent = -1,
        eShimEnrolledEvent,         /* 3: Shim Enrolled: network Interface Initialized*/
        eStackTxEvent,              /* 5: The software stack IPCP has queued a packet to transmit. */
        eFATimerEvent,              /* 6: See if any IPCP socket needs attention. */
        eFlowBindEvent,             /* 7: Client API request to bind a flow. */
        eFlowDeallocateEvent,       /* 8: A flow must be deallocated */
        eStackRxEvent,              /* 9: The stack IPCP has queued a packet to received */
        eShimFATimerEvent,          /* 10: A flow has been allocated on the shimWiFi*/
        eStackFlowAllocateEvent,    /* 11: The Software stack IPCP has received a Flow allocate request. */
        eStackAppRegistrationEvent, /* 12: The Software stack IPCP has received a AppRegistration Event*/
        eShimAppRegisteredEvent,    /* 13: The Normal IPCP has been registered into the Shim*/
        eSendMgmtEvent,             /* 14: Send Mgmt PDU */

    } eRINAEvent_t;

    /**
     * Structure for the information of the commands issued to the RINA task.
     */
    typedef struct xRINA_TASK_COMMANDS
    {
        eRINAEvent_t eEventType; /**< The event-type enum */
        union
        {
            void *PV;
            uint32_t UN;
            int32_t N;
            char C;
            uint8_t B;
        } xData; /**< The data in the event */
    } RINAStackEvent_t;

#ifdef __cplusplus
}
#endif

#endif // _COMPONENTS_IPCP_EVENTS_H
