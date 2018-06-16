/*
 * WordMatcher.{cc,hh} -- remove insults in web pages
 * Romain Gaillard
 * Tom Barbette
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "wordmatcher.hh"
#include "tcpelement.hh"

CLICK_DECLS

WordMatcher::WordMatcher() : insults()
{
    // Initialize the memory pool of each thread
    for(unsigned int i = 0; i < poolBufferEntries.weight(); ++i)
        poolBufferEntries.get_value(i).initialize(POOL_BUFFER_ENTRIES_SIZE);

    closeAfterInsults = false;
    _mask = true;
    _insert = false;
    _full = false;
    _all = false;
}

int WordMatcher::configure(Vector<String> &conf, ErrorHandler *errh)
{
    //TODO : use a proper automaton for insults
    _insert_msg = "<font color='red'>Blocked content !</font><br />";
    String mode = "MASK";
    bool all = false;
    if(Args(conf, this, errh)
            .read_all("WORD", insults)
            .read_p("MODE", mode)
            .read_p("MSG", _insert_msg)
            .read_p("CLOSECONNECTION", closeAfterInsults)
            .read("ALL",all)
    .complete() < 0)
        return -1;

    if (all) {
        _all = all;
        errh->warning("Element not optimized for ALL");
    }
    _mask = false;
    if (mode == "CLOSE") {
        _insert = false;
        closeAfterInsults = true;
    } else if (mode == "MASK") {
        _mask = true;
        _insert = false;
    } else if (mode == "REMOVE") {
        _insert = false;
    } else if (mode == "REPLACE") {
        _insert = true;
    } else if (mode == "FULL") {
        _insert = false;
        _full = true;
    } else {
        return errh->error("Mode must be MASK, REMOVE, REPLACE or FULL");
    }


    if (insults.size() == 0) {
        return errh->error("No words given");
    }

    return 0;
}

int
WordMatcher::maxModificationLevel() {
    int mod = StackSpaceElement<fcb_WordMatcher>::maxModificationLevel() | MODIFICATION_WRITABLE;
    if (!_mask) {
        mod |= MODIFICATION_RESIZE | MODIFICATION_STALL;
    } else {
        mod |= MODIFICATION_REPLACE;
    }
    return mod;
}


void WordMatcher::push_batch(int port, fcb_WordMatcher* WordMatcher, PacketBatch* flow)
{
    WordMatcher->flowBuffer.enqueueAll(flow);

    auto iter = WordMatcher->flowBuffer.contentBegin();
    if (!iter.current()) {
        goto finished;
    }
    /**
     * This is mostly an example element, so we have two modes :
     * - Replacement, done inline using the iterator directly in this element
     * - Removing, done calling iterator.remove
     */
    for(int i = 0; i < insults.size(); ++i)
    {
        const char* insult = insults[i].c_str();
        if (_mask) { //Masking mode
            auto end = WordMatcher->flowBuffer.contentEnd();

            while (iter != end) {
                if (*iter ==  insult[0]) {
                    int pos = 0;
                    typeof(iter) start_pos = iter;
                    do {
                        ++pos;
                        ++iter;
                        if (iter == end) {
                            //Finished in the middle of a potential match, ask for more packets
                            flow = start_pos.flush();
                            if(!isLastUsefulPacket(start_pos.current())) {
                                requestMorePackets(start_pos.current(), false);
                                goto needMore;
                            } else {
                                goto finished;
                            }
                        }
                        if (insult[pos] == '\0') {
                            WordMatcher->counterRemoved += 1;
                            if (closeAfterInsults)
                                goto closeconn;
                            pos = 0;
                            while (insult[pos] != '\0') {
                                *start_pos = '*';
                                ++start_pos;
                                ++pos;
                            }
                            pos = 0;
                        }
                    } while (*iter == insult[pos]);
                }
                ++iter;
            }
        } else {
            int result;
            FlowBufferContentIter iter;
            do {
                //iter = WordMatcher->flowBuffer.search(iter, insult, &result);
                iter = WordMatcher->flowBuffer.searchSSE(iter, insult, insults[i].length(), &result);
                //click_chatter("Found %d at %d,",result,iter.current()?iter.leftInChunk():-1);
                if (result == 1) {
                    if (!_insert) { //If not insert, just remove
                        WordMatcher->flowBuffer.remove(iter,insults[i].length(), this);
                    } else if (!_full){ //Insert but not full, replace pattern per message
                        WordMatcher->flowBuffer.replaceInFlow(iter, insults[i].length(), _insert_msg.c_str(), _insert_msg.length(), this);
                    }
                }
                if (result == 1) {
                    if (closeAfterInsults)
                        goto closeconn;
                    if (_full) {
                        //TODO
                        //Remove all bytes
                        //Add msg
                    }
                    WordMatcher->counterRemoved += 1;
                }
            } while (_all && result == 1);

            // While we keep finding complete insults in the packet
            if (result == 0) { //Finished in the middle of a potential match
                if(!isLastUsefulPacket(flow->tail())) {
                    requestMorePackets(flow->tail(), false);
                    flow = WordMatcher->flowBuffer.dequeueUpTo(iter.current());
                    // We will re-match the whole last packet, see FlowIDSMatcher for better implementation
                    goto needMore;
                } else {
                    goto finished;
                }
            }
        }
    }
    finished:
    //Finished without being in the middle of an insult. If closeconn was set and there was an insult, we already jumped further.
    output_push_batch(0, WordMatcher->flowBuffer.dequeueAll());
    return;

    closeconn:

    closeConnection(flow, true);
    WordMatcher->flowBuffer.dequeueAll()->fast_kill();

    return;

    needMore:


    if(flow != NULL)
        output_push_batch(0, flow);
    return;
}


CLICK_ENDDECLS
EXPORT_ELEMENT(WordMatcher)
ELEMENT_MT_SAFE(WordMatcher)
