import itertools
import logging
import numpy
import random

from omnetpp.scave.results import *

from inet.simulation.project import *
from inet.test.run import *

logger = logging.getLogger(__name__)

def compare_test_results(result1, result2, accuracy = 0.01):
    return abs(result1 - result2) / result1 < accuracy

############################
# TSN frame replication test

def run_tsn_framereplication_simulation(**kwargs):
    return run_simulation(working_directory = "/tests/validation/tsn/framereplication/", sim_time_limit = "0.1s", print_end = " ", **kwargs)

def compute_frame_replication_success_rate_from_simulation_results(**kwargs):
    filter_expression = """type =~ scalar AND ((module =~ "*.destination.udp" AND name =~ packetReceived:count) OR (module =~ "*.source.udp" AND name =~ packetSent:count))"""
    df = read_result_files(inet_project.get_full_path("tests/validation/tsn/framereplication/results/*.sca"), filter_expression = filter_expression)
    df = get_scalars(df)
    packetSent = float(df[df.name == "packetSent:count"].value)
    packetReceived = float(df[df.name == "packetReceived:count"].value)
    return packetReceived / packetSent

def compute_frame_replication_success_rate_analytically1():
    combinations = numpy.array(list(itertools.product([0, 1], repeat=7)))
    probabilities = numpy.array([0.8, 0.8, 0.64, 0.8, 0.64, 0.8, 0.8])
    solutions = numpy.array([[1, 1, 1, 0, 0, 0, 0], [1, 1, 0, 0, 1, 1, 0], [1, 0, 0, 1, 1, 0, 0,], [1, 0, 1, 1, 0, 0, 1]])
    p = 0
    for combination in combinations:
        probability = (combination * probabilities + (1 - combination) * (1 - probabilities)).prod()
        for solution in solutions:
            if (solution * combination == solution).all() :
                p += probability
                break   
    return p

def compute_frame_replication_success_rate_analytically2():
    successful = 0
    n = 1000000
    for i in range(n):
        s1 = random.random() < 0.8
        s1_s2a = random.random() < 0.8
        s1_s2b = random.random() < 0.8
        s2a_s2b = random.random() < 0.8
        s2b_s2a = random.random() < 0.8
        s2a = (s1 and s1_s2a) or (s1 and s1_s2b and s2b_s2a)
        s2b = (s1 and s1_s2b) or (s1 and s1_s2a and s2a_s2b)
        s3a = s2a and (random.random() < 0.8)
        s3b = s2b and (random.random() < 0.8)
        s3a_s4 = random.random() < 0.8
        s3b_s4 = random.random() < 0.8
        s4 = (s3a and s3a_s4) or (s3b and s3b_s4)
        if s4:
            successful += 1
    return successful / n

def compute_tsn_framereplication_validation_test_results(test_accuracy = 0.01, **kwargs):
    ps = compute_frame_replication_success_rate_from_simulation_results(**kwargs)
    pa1 = compute_frame_replication_success_rate_analytically1()
    pa2 = compute_frame_replication_success_rate_analytically2()
    test_result1 = compare_test_results(ps, pa1, test_accuracy)
    test_result2 = compare_test_results(ps, pa2, test_accuracy)
    return TestResult(None, None, bool_result=test_result1 and test_result2)

def run_tsn_framereplication_validation_test(test_accuracy = 0.01, **kwargs):
    run_tsn_framereplication_simulation(**kwargs)
    print(compute_tsn_framereplication_validation_test_results(**kwargs).get_description())

###################################################
# TSN traffic shaping asynchronous shaper ICCT test

def run_tsn_trafficshaping_asynchronousshaper_icct_simulation(**kwargs):
    run_simulation(working_directory = "/tests/validation/tsn/trafficshaping/asynchronousshaper/icct", sim_time_limit = "0.1s", print_end = " ", **kwargs)

def compute_asynchronousshaper_icct_endtoend_delay_from_simulation_results(**kwargs):
    filter_expression = """type =~ scalar AND name =~ meanBitLifeTimePerPacket:histogram:max"""
    df = read_result_files(inet_project.get_full_path("tests/validation/tsn/trafficshaping/asynchronousshaper/icct/results/*.sca"), filter_expression = filter_expression, include_fields_as_scalars = True)
    df = get_scalars(df)
    df["name"] = df["name"].map(lambda name: re.sub(".*(min|max)", "\\1", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*N6.app\\[[0-4]\\].*", "Flow 4, Class A", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*N6.app\\[[5-9]\\].*", "Flow 5, Class B", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*N7.app\\[[0-9]\\].*", "Flow 1, CDT", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*N7.app\\[1[0-9]\\].*", "Flow 2, Class A", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*N7.app\\[2[0-9]\\].*", "Flow 3, Class B", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*N7.app\\[3[0-4]\\].*", "Flow 6, Class A", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*N7.app\\[3[5-9]\\].*", "Flow 7, Class B", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*N7.app\\[40\\].*", "Flow 8, Best Effort", name))
    df = pd.pivot_table(df, index="module", columns="name", values="value", aggfunc=max)
    return df * 1000000

def compute_asynchronousshaper_icct_endtoend_delay_alternatively():
    # This validation test compares simulation results to analytical results presented
    # in the paper titled "The Delay Bound Analysis Based on Network Calculus for
    # Asynchronous Traffic Shaping under Parameter Inconsistency" from the 2020 IEEE
    # 20th International Conference on Communication Technology
    return pd.DataFrame(index = ["Flow 1, CDT", "Flow 2, Class A", "Flow 3, Class B", "Flow 4, Class A", "Flow 5, Class B", "Flow 6, Class A", "Flow 7, Class B", "Flow 8, Best Effort"],
                        data = {"max": [867.6, 4665.5, 10902, 2505.2, 5678.3, float('inf'), float('inf'), float('inf')]})

def compute_tsn_trafficshaping_asynchronousshaper_icct_validation_test_results(**kwargs):
    df1 = compute_asynchronousshaper_icct_endtoend_delay_from_simulation_results(**kwargs)
    df2 = compute_asynchronousshaper_icct_endtoend_delay_alternatively()
    test_result = (df1["max"] < df2["max"]).all()
    return TestResult(None, None, bool_result=test_result)

def run_tsn_trafficshaping_asynchronousshaper_icct_validation_test(**kwargs):
    run_tsn_trafficshaping_asynchronousshaper_icct_simulation(**kwargs)
    print(compute_tsn_trafficshaping_asynchronousshaper_icct_validation_test_results(**kwargs).get_description())

########################################################
# TSN traffic shaping asynchronous shaper Core4INET test

def run_tsn_trafficshaping_asynchronousshaper_core4inet_simulation(**kwargs):
    return run_simulation(working_directory = "/tests/validation/tsn/trafficshaping/asynchronousshaper/core4inet", sim_time_limit = "1s", print_end = " ", **kwargs)

def compute_asynchronousshaper_core4inet_endtoend_delay_from_simulation_results(**kwargs):
    filter_expression = """type =~ scalar AND (name =~ meanBitLifeTimePerPacket:histogram:min OR name =~ meanBitLifeTimePerPacket:histogram:max OR name =~ meanBitLifeTimePerPacket:histogram:mean OR name =~ meanBitLifeTimePerPacket:histogram:stddev)"""
    df = read_result_files(inet_project.get_full_path("tests/validation/tsn/trafficshaping/asynchronousshaper/core4inet/results/*.sca"), filter_expression = filter_expression, include_fields_as_scalars = True)
    df = get_scalars(df)
    df["name"] = df["name"].map(lambda name: re.sub(".*(min|max|mean|stddev)", "\\1", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*app\\[0\\].*", "Best effort", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*app\\[1\\].*", "Medium", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*app\\[2\\].*", "High", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*app\\[3\\].*", "Critical", name))
    df = df.loc[df["module"]!="Best effort"]
    df = pd.pivot_table(df, index="module", columns="name", values="value")
    return df * 1000000

def compute_asynchronousshaper_core4inet_max_queuelength_from_simulation_results(**kwargs):
    filter_expression = """type =~ scalar AND module =~ \"*.switch.eth[4].macLayer.queue.queue[5..7]\" AND name =~ queueLength:max"""
    df = read_result_files(inet_project.get_full_path("tests/validation/tsn/trafficshaping/asynchronousshaper/core4inet/results/*.sca"), filter_expression = filter_expression, include_fields_as_scalars = True)
    df = get_scalars(df)
    return numpy.max(df["value"])

def compute_asynchronousshaper_core4inet_endtoend_delay_alternatively(**kwargs):
    # This validation test compares simulation results to analytical results and also
    # to results from a different simulation using Core4INET.
    # https://github.com/CoRE-RG/CoRE4INET/tree/master/examples/tsn/medium_network
    return pd.DataFrame(index = ["Medium", "High", "Critical"],
                        data = {"min": [88.16, 60.8, 252.8],
                                "max": [540, 307.2, 375.84],
                                "mean": [247.1, 161.19, 298.52],
                                "stddev": [106.53, 73.621, 36.633]})

def compute_tsn_trafficshaping_asynchronousshaper_core4inet_validation_test_results(test_accuracy = 0.01, **kwargs):
    df1 = compute_asynchronousshaper_core4inet_endtoend_delay_from_simulation_results(**kwargs)
    df2 = compute_asynchronousshaper_core4inet_endtoend_delay_alternatively(**kwargs)
    df1 = df1.sort_index(axis = 0).sort_index(axis = 1)
    df2 = df2.sort_index(axis = 0).sort_index(axis = 1)
    maxQueueLength = compute_asynchronousshaper_core4inet_max_queuelength_from_simulation_results()
    test_result = maxQueueLength < 4 and \
                  (df1["min"] > df2["min"]).all() and \
                  (df1["max"] < df2["max"]).all() and \
                  numpy.allclose(df1["min"], df2["min"], rtol=test_accuracy, atol=0) and \
                  numpy.allclose(df1["max"], df2["max"], rtol=test_accuracy, atol=0) and \
                  numpy.allclose(df1["mean"], df2["mean"], rtol=test_accuracy * 7, atol=0) and \
                  numpy.allclose(df1["stddev"], df2["stddev"], rtol=test_accuracy * 30, atol=0)
    return TestResult(None, None, bool_result=test_result)

def run_tsn_trafficshaping_asynchronousshaper_core4inet_validation_test(test_accuracy = 0.01, **kwargs):
    run_tsn_trafficshaping_asynchronousshaper_core4inet_simulation(**kwargs)
    print(compute_tsn_trafficshaping_asynchronousshaper_core4inet_validation_test_results(**kwargs).get_description())

##############################################
# TSN traffic shaping credit-based shaper test

def run_tsn_trafficshaping_creditbasedshaper_simulation(**kwargs):
    return run_simulation(working_directory = "/tests/validation/tsn/trafficshaping/creditbasedshaper", sim_time_limit = "1s", print_end = " ", **kwargs)

def compute_creditbasedshaper_endtoend_delay_from_simulation_results(**kwargs):
    filter_expression = """type =~ scalar AND (name =~ meanBitLifeTimePerPacket:histogram:min OR name =~ meanBitLifeTimePerPacket:histogram:max OR name =~ meanBitLifeTimePerPacket:histogram:mean OR name =~ meanBitLifeTimePerPacket:histogram:stddev)"""
    df = read_result_files(inet_project.get_full_path("tests/validation/tsn/trafficshaping/creditbasedshaper/results/*.sca"), filter_expression = filter_expression, include_fields_as_scalars = True)
    df = get_scalars(df)
    df["name"] = df["name"].map(lambda name: re.sub(".*(min|max|mean|stddev)", "\\1", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*app\\[0\\].*", "Best effort", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*app\\[1\\].*", "Medium", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*app\\[2\\].*", "High", name))
    df["module"] = df["module"].map(lambda name: re.sub(".*app\\[3\\].*", "Critical", name))
    df = df.loc[df["module"]!="Best effort"]
    df = pd.pivot_table(df, index="module", columns="name", values="value")
    return df * 1000000

def compute_creditbasedshaper_max_queuelength_from_simulation_results(**kwargs):
    filter_expression = """type =~ scalar AND module =~ \"*.switch.eth[4].macLayer.queue.queue[5..7]\" AND name =~ queueLength:max"""
    df = read_result_files(inet_project.get_full_path("tests/validation/tsn/trafficshaping/creditbasedshaper/results/*.sca"), filter_expression = filter_expression, include_fields_as_scalars = True)
    df = get_scalars(df)
    return numpy.max(df["value"])

def compute_creditbasedshaper_endtoend_delay_alternatively(**kwargs):
    # This validation test compares simulation results to analytical results and also
    # to results from a different simulation using Core4INET.
    # https://github.com/CoRE-RG/CoRE4INET/tree/master/examples/tsn/medium_network
    return pd.DataFrame(index = ["Medium", "High", "Critical"],
                        data = {"min": [88.16, 60.8, 252.8],
                                "max": [540, 307.2, 375.84],
                                "mean": [247.1, 161.19, 298.52],
                                "stddev": [106.53, 73.621, 36.633]})

def compute_tsn_trafficshaping_creditbasedshaper_validation_test_results(test_accuracy = 0.01, **kwargs):
    df1 = compute_creditbasedshaper_endtoend_delay_from_simulation_results(**kwargs)
    df2 = compute_creditbasedshaper_endtoend_delay_alternatively(**kwargs)
    df1 = df1.sort_index(axis = 0).sort_index(axis = 1)
    df2 = df2.sort_index(axis = 0).sort_index(axis = 1)
    maxQueueLength = compute_creditbasedshaper_max_queuelength_from_simulation_results()
    test_result = maxQueueLength < 4 and \
                  (df1["min"] > df2["min"]).all() and \
                  (df1["max"] < df2["max"]).all() and \
                  numpy.allclose(df1["min"], df2["min"], rtol=test_accuracy, atol=0) and \
                  numpy.allclose(df1["max"], df2["max"], rtol=test_accuracy, atol=0) and \
                  numpy.allclose(df1["mean"], df2["mean"], rtol=test_accuracy * 7, atol=0) and \
                  numpy.allclose(df1["stddev"], df2["stddev"], rtol=test_accuracy * 30, atol=0)
    return TestResult(None, None, bool_result=test_result)

def run_tsn_trafficshaping_creditbasedshaper_validation_test(**kwargs):
    run_tsn_trafficshaping_creditbasedshaper_simulation(**kwargs)
    print(compute_tsn_trafficshaping_creditbasedshaper_validation_test_results(**kwargs).get_description())

def run_tsn_trafficshaping_asynchronousshaper_validation_tests(**kwargs):
    run_tsn_trafficshaping_asynchronousshaper_icct_validation_test(**kwargs)
    run_tsn_trafficshaping_asynchronousshaper_core4inet_validation_test(**kwargs)

def run_tsn_trafficshaping_validation_tests(**kwargs):
    run_tsn_trafficshaping_asynchronousshaper_validation_tests(**kwargs)
    run_tsn_trafficshaping_creditbasedshaper_validation_test(**kwargs)

def run_tsn_validation_tests(**kwargs):
    run_tsn_framereplication_validation_test(**kwargs)
    run_tsn_trafficshaping_validation_tests(**kwargs)

def run_validation_tests(**kwargs):
    logger.info("Running validation tests")
    run_tsn_validation_tests(**kwargs)
