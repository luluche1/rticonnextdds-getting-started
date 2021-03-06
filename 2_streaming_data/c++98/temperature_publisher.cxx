/*
 * (c) Copyright, Real-Time Innovations, 2020.  All rights reserved.
 * RTI grants Licensee a license to use, modify, compile, and create derivative
 * works of the software solely for use with RTI Connext DDS. Licensee may
 * redistribute copies of the software provided that all such copies are subject
 * to this license. The software is provided "as is", with no warranty of any
 * type, including any warranty for fitness for any purpose. RTI is under no
 * obligation to maintain or support the software. RTI shall not be liable for
 * any incidental or consequential damages arising out of the use or inability
 * to use the software.
 */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#include "temperature.h"
#include "temperatureSupport.h"
#include "ndds/ndds_cpp.h"
#include "application.h"

using namespace application;

static int shutdown(
        DDSDomainParticipant *participant,
        const char *shutdown_message,
        int status);

int run_example(
        unsigned int domain_id,
        unsigned int sample_count,
        const char *sensor_id)
{
    // Connext DDS setup
    // -----------------
    // A DomainParticipant allows an application to begin communicating in
    // a DDS domain. Typically there is one DomainParticipant per application.
    // DomainParticipant QoS is configured in USER_QOS_PROFILES.xml
    DDSDomainParticipant *participant =
            DDSTheParticipantFactory->create_participant(
                    domain_id,
                    DDS_PARTICIPANT_QOS_DEFAULT,
                    NULL /* listener */,
                    DDS_STATUS_MASK_NONE);
    if (participant == NULL) {
        return shutdown(participant, "create_participant error", EXIT_FAILURE);
    }

    // A Publisher allows an application to create one or more DataWriters
    // Publisher QoS is configured in USER_QOS_PROFILES.xml
    DDSPublisher *publisher = participant->create_publisher(
            DDS_PUBLISHER_QOS_DEFAULT,
            NULL /* listener */,
            DDS_STATUS_MASK_NONE);
    if (publisher == NULL) {
        return shutdown(participant, "create_publisher error", EXIT_FAILURE);
    }

    // Register the datatype to use when creating the Topic
    const char *type_name = TemperatureTypeSupport::get_type_name();
    DDS_ReturnCode_t retcode =
            TemperatureTypeSupport::register_type(participant, type_name);
    if (retcode != DDS_RETCODE_OK) {
        return shutdown(participant, "register_type error", EXIT_FAILURE);
    }

    // A Topic has a name and a datatype. Create a Topic called
    // "ChocolateTemperature" with your registered data type
    DDSTopic *topic = participant->create_topic(
            "ChocolateTemperature",
            type_name,
            DDS_TOPIC_QOS_DEFAULT,
            NULL /* listener */,
            DDS_STATUS_MASK_NONE);
    if (topic == NULL) {
        return shutdown(participant, "create_topic error", EXIT_FAILURE);
    }

    // This DataWriter writes data on Topic "ChocolateTemperature"
    // DataWriter QoS is configured in USER_QOS_PROFILES.xml
    DDSDataWriter *writer = publisher->create_datawriter(
            topic,
            DDS_DATAWRITER_QOS_DEFAULT,
            NULL /* listener */,
            DDS_STATUS_MASK_NONE);
    if (writer == NULL) {
        return shutdown(participant, "create_datawriter error", EXIT_FAILURE);
    }

    // A narrow is a cast from a generic DataWriter to one that is specific
    // to your type. Use the type specific DataWriter to write()
    TemperatureDataWriter *Temperature_writer =
            TemperatureDataWriter::narrow(writer);
    if (Temperature_writer == NULL) {
        return shutdown(participant, "DataWriter narrow error", EXIT_FAILURE);
    }

    // Create data sample for writing
    Temperature *sample = TemperatureTypeSupport::create_data();
    if (sample == NULL) {
        return shutdown(
                participant,
                "TemperatureTypeSupport::create_data error",
                EXIT_FAILURE);
    }

    // Main loop, write data
    // ---------------------
    for (unsigned int count = 0;
         running && ((sample_count == 0) || (count < sample_count));
         ++count) {
        // Modify the data to be written here
        snprintf(sample->sensor_id, 255, "%s", sensor_id);
        sample->degrees = rand() % 3 + 30;  // Random number between 30 and 32


        std::cout << "Writing ChocolateTemperature, count " << count
                  << std::endl;
        retcode = Temperature_writer->write(*sample, DDS_HANDLE_NIL);
        if (retcode != DDS_RETCODE_OK) {
            std::cerr << "write error " << retcode << std::endl;
        }

        // Exercise: Change this to sleep 10 ms in between writing temperatures
        DDS_Duration_t send_period = { 4, 0 };
        NDDSUtility::sleep(send_period);
    }

    // Cleanup
    // -------
    // Delete data sample
    retcode = TemperatureTypeSupport::delete_data(sample);
    if (retcode != DDS_RETCODE_OK) {
        std::cerr << "TemperatureTypeSupport::delete_data error " << retcode
                  << std::endl;
    }

    // Delete all entities (DataWriter, Topic, Publisher, DomainParticipant)
    return shutdown(participant, "shutting down", EXIT_SUCCESS);
}

// Delete all entities
static int shutdown(
        DDSDomainParticipant *participant,
        const char *shutdown_message,
        int status)
{
    DDS_ReturnCode_t retcode;

    std::cout << shutdown_message << std::endl;

    if (participant != NULL) {
        // This includes everything created by this Participant, including
        // DataWriters, Topics, Publishers. (and Subscribers and DataReaders)
        retcode = participant->delete_contained_entities();
        if (retcode != DDS_RETCODE_OK) {
            std::cerr << "delete_contained_entities error " << retcode
                      << std::endl;
            status = EXIT_FAILURE;
        }

        retcode = DDSTheParticipantFactory->delete_participant(participant);
        if (retcode != DDS_RETCODE_OK) {
            std::cerr << "delete_participant error " << retcode << std::endl;
            status = EXIT_FAILURE;
        }
    }

    return status;
}

// Sets Connext verbosity to help debugging
void set_verbosity(unsigned int verbosity)
{
    NDDSConfigLogger::get_instance()->set_verbosity(
            static_cast<NDDS_Config_LogVerbosity>(verbosity));
}

int main(int argc, char *argv[])
{
    // Parse arguments and handle control-C
    ApplicationArguments arguments;
    parse_arguments(arguments, argc, argv);
    if (arguments.parse_result == PARSE_RETURN_EXIT) {
        return EXIT_SUCCESS;
    } else if (arguments.parse_result == PARSE_RETURN_FAILURE) {
        return EXIT_FAILURE;
    }
    setup_signal_handlers();

    // Enables different levels of debugging output
    set_verbosity(arguments.verbosity);

    int status = run_example(
            arguments.domain_id,
            arguments.sample_count,
            arguments.sensor_id);

    // Releases the memory used by the participant factory.  Optional at
    // application shutdown
    DDS_ReturnCode_t retcode = DDSDomainParticipantFactory::finalize_instance();
    if (retcode != DDS_RETCODE_OK) {
        std::cerr << "finalize_instance error " << retcode << std::endl;
        status = EXIT_FAILURE;
    }

    return status;
}
