/*
 * Copyright (c) 2011, Intel Corporation.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <stdio.h>
#include <stdarg.h>
#include "frmwkEx.h"
#include "tnvme.h"
#include "globals.h"
#include "../Utils/kernelAPI.h"
#include "../Utils/io.h"
#include "../Cmds/getLogPage.h"

#define GRP_NAME            "post"
#define TEST_NAME           "failure"


FrmwkEx::FrmwkEx()
{
    LOG_ERR("FAILURE:no reason supplied");
    PreliminaryProcessing();    // Override in children provides custom logic
    gCtrlrConfig->SetState(ST_DISABLE);
}


FrmwkEx::FrmwkEx(string &msg)
{
    mMsg = msg;
    LOG_ERR("FAILURE:%s", mMsg.c_str());
    PreliminaryProcessing();    // Override in children provides custom logic
    gCtrlrConfig->SetState(ST_DISABLE);
}


FrmwkEx::FrmwkEx(const char *fmt, ...)
{
    char work[256];
    va_list arg;

    va_start(arg, fmt);
    vsnprintf(work, sizeof(work), fmt, arg);
    va_end(arg);

    mMsg = work;
    LOG_ERR("FAILURE:%s", mMsg.c_str());
    PreliminaryProcessing();    // Override in children provides custom logic
    gCtrlrConfig->SetState(ST_DISABLE);
}


FrmwkEx::~FrmwkEx()
{
}


void
FrmwkEx::PreliminaryProcessing()
{
    LOG_NRM("--------START POST FAILURE STATE DUMP--------");
    try {
        // First gather all non-intrusive data, things that won't change the
        // state of the DUT, effectively taking a snapshot.
        KernelAPI::DumpKernelMetrics(FileSystem::PrepLogFile(GRP_NAME,
            TEST_NAME, "kmetrics"));
        KernelAPI::DumpPciSpaceRegs(FileSystem::PrepLogFile(GRP_NAME,
            TEST_NAME, "pci", "regs"), false);
        KernelAPI::DumpCtrlrSpaceRegs(FileSystem::PrepLogFile(GRP_NAME,
            TEST_NAME, "ctrl", "regs"), false);

        // Now we can change the state of the DUT. We don't know the state of
        // the ACQ/ASQ, or even if there are any in existence; place the DUT
        // into a well known state and then interact gathering instrusive data
        if (gCtrlrConfig->SetState(ST_DISABLE_COMPLETELY) == false) {
            throw FrmwkEx();
        }

        SharedACQPtr acq = SharedACQPtr(new ACQ(gInformative->GetFD()));
        acq->Init(2);

        SharedASQPtr asq = SharedASQPtr(new ASQ(gInformative->GetFD()));
        asq->Init(2);

        if (gCtrlrConfig->SetState(ST_ENABLE) == false)
            throw FrmwkEx();

        LOG_NRM("Create get log page cmd and assoc some buffer memory");
        SharedGetLogPagePtr getLogPg = SharedGetLogPagePtr(new GetLogPage());
        getLogPg->SetLID(GetLogPage::LOGID_ERROR_INFO);

        ConstSharedIdentifyPtr idCmdCtrlr = gInformative->GetIdentifyCmdCtrlr();
        uint64_t errLogPgEntries = (idCmdCtrlr->GetValue(IDCTRLRCAP_ELPE) + 1);
        uint32_t bufSize = (errLogPgEntries * GetLogPage::ERRINFO_DATA_SIZE);
        uint16_t numDWAvail = (bufSize / sizeof(uint32_t));
        getLogPg->SetNUMD(numDWAvail);

        SharedMemBufferPtr cmdMem = SharedMemBufferPtr(new MemBuffer());
        cmdMem->InitAlignment(bufSize, sysconf(_SC_PAGESIZE), true, 0);
        send_64b_bitmask prpReq =
            (send_64b_bitmask)(MASK_PRP1_PAGE | MASK_PRP2_PAGE);
        getLogPg->SetPrpBuffer(prpReq, cmdMem);

        IO::SendCmdToHdw(GRP_NAME, TEST_NAME, 2000, asq, acq, getLogPg, "",
            false);
        getLogPg->Dump(FileSystem::PrepLogFile(GRP_NAME, TEST_NAME,
            "LogPageErr"), "Failed test post dump of error log page:");
    } catch (...) {
        LOG_NRM("This 2ndary failure could be caused by a prior");
    }
}

