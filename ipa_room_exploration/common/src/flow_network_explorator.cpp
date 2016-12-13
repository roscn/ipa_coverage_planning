#include <ipa_room_exploration/flow_network_explorator.h>

// Constructor
flowNetworkExplorator::flowNetworkExplorator()
{

}

// Function that creates a Qsopt optimization problem and solves it, using the given matrices and vectors and the multistage
// ansatz, that determines in each stage exactly one node that is visited.
template<typename T>
void flowNetworkExplorator::solveMultiStageOptimizationProblem(std::vector<T>& C, const cv::Mat& V, const std::vector<double>& weights,
		const std::vector<std::vector<uint> >& flows_into_nodes, const std::vector<std::vector<uint> >& flows_out_of_nodes,
		const int stages, const std::vector<uint>& start_arcs,  const std::vector<double>* W)
{
	// initialize the problem
	QSprob problem;
	problem = QScreate_prob("flowNetworkExploration", QS_MIN);

	std::cout << "Creating and solving linear program." << std::endl;

	// add the optimization variables to the problem at each stage, stages zero induced
	int rval;
	for(size_t r=0; r<stages; ++r)
	{
		// at initial stage only add arcs going out from the start node
		if(r==0)
		{
			for(size_t arc=0; arc<start_arcs.size(); ++arc)
			{
				if(W != NULL) // if a relaxation-vector is provided, use it to set the weights for the variables
					rval = QSnew_col(problem, W->operator[](arc)*weights[start_arcs[arc]], 0.0, 1.0, (const char *) NULL);
				else
					rval = QSnew_col(problem, weights[start_arcs[arc]], 0.0, 1.0, (const char *) NULL);
			}
		}
		else
		{
			for(size_t variable=0; variable<V.cols; ++variable) // columns of V determine number of arcs at one stage
			{
				if(W != NULL) // if a weight-vector is provided, use it to set the weights for the variables
					rval = QSnew_col(problem, W->operator[](variable + start_arcs.size() + V.cols*(r-1))*weights[variable], 0.0, 1.0, (const char *) NULL);
				else
					rval = QSnew_col(problem, weights[variable], 0.0, 1.0, (const char *) NULL);

				if(rval)
					std::cout << "!!!!! failed to add variable !!!!!" << std::endl;
			}
		}
	}
	int number_of_variables = QSget_colcount(problem);
	std::cout << "number of variables in the problem: " << number_of_variables << std::endl;

	// inequality constraints to ensure that every position has been seen at least once:
	//		for each center that should be covered, find the arcs of the different stages that cover it
	for(size_t row=0; row<V.rows; ++row)
	{
		std::vector<int> variable_indices;

		// initial stage
		for(size_t col=0; col<start_arcs.size(); ++col)
			if(V.at<uchar>(row, start_arcs[col])==1)
				variable_indices.push_back((int) col);

		// further stages
		for(size_t col=0; col<V.cols; ++col)
			if(V.at<uchar>(row, col) == 1)
				for(size_t r=1; r<stages; ++r)
					variable_indices.push_back((int) col + start_arcs.size() + V.cols*(r-1));

		// all indices are 1 in this constraint
		std::vector<double> variable_coefficients(variable_indices.size(), 1.0);

		// add the constraint, if the current cell can be covered by the given arcs
		if(variable_indices.size()>0)
			rval = QSadd_row(problem, (int) variable_indices.size(), &variable_indices[0], &variable_coefficients[0], 1.0, 'G', (const char *) NULL);

		if(rval)
			std::cout << "!!!!! failed to add constraint !!!!!" << std::endl;
	}

	// constraints to ensure that only one arc at every stage is taken
	for(size_t r=0; r<stages; ++r)
	{
		std::vector<int> variable_indices;

		if(r==0) // initial stage
			for(size_t variable=0; variable<start_arcs.size(); ++variable)
				variable_indices.push_back(variable);
		else // other stages
			for(size_t variable=0; variable<V.cols; ++variable)
				variable_indices.push_back(variable + start_arcs.size() + V.cols*(r-1));

		// all indices are 1 in this constraint
		std::vector<double> variable_coefficients(variable_indices.size(), 1.0);

		// add constraint for current stage
		if(r==0)
			rval = QSadd_row(problem, (int) variable_indices.size(), &variable_indices[0], &variable_coefficients[0], 1.0, 'E', (const char *) NULL);
		else
			rval = QSadd_row(problem, (int) variable_indices.size(), &variable_indices[0], &variable_coefficients[0], 1.0, 'E', (const char *) NULL);

		if(rval)
			std::cout << "!!!!! failed to add constraint !!!!!" << std::endl;

	}

	// equality constraint to ensure that if in the previous stage an arc flows into one node, another arc flowing out of
	// the node is taken
	//	Remark:	not done for initial and final stage, because at the initial step there is no previous step and the path
	//			shouldn't be a cycle, like in a traveling salesman problem
	for(size_t r=1; r<stages-1; ++r)
	{
		for(size_t node=0; node<flows_into_nodes.size(); ++node)
		{
			std::vector<int> variable_indices;
			std::vector<double> variable_coefficients;

			// gather flows into node
			for(size_t inflow=0; inflow<flows_into_nodes[node].size(); ++inflow)
			{
				// if at stage one a start arcs flows into the node, take the index of the arc in the start_arcs
				// vector
				if(r==1 && contains(start_arcs, flows_into_nodes[node][inflow])==true)
				{
					variable_indices.push_back(std::find(start_arcs.begin(), start_arcs.end(), flows_into_nodes[node][inflow])-start_arcs.begin());
					variable_coefficients.push_back(1.0);
				}
				// if the incoming arc is not an initial arc, get its index in the optimization vector
				else if(r>1)
				{
					variable_indices.push_back(flows_into_nodes[node][inflow] + start_arcs.size() + V.cols*(r-2));
					variable_coefficients.push_back(1.0);
				}
			}

			// if the current node has no incoming arc, ignore it in this step
			//	Remark: should only hold if r=1 and the start arcs don't go into this node
			if(variable_coefficients.size()==0)
				continue;

			// gather flows out of node
			for(size_t outflow=0; outflow<flows_out_of_nodes[node].size(); ++outflow)
			{
				variable_indices.push_back(flows_out_of_nodes[node][outflow] + start_arcs.size() + V.cols*(r-1));
				variable_coefficients.push_back(-1.0);
			}

// 			testing
//			std::cout << "number of flows: " << variable_indices.size() << std::endl;
//			for(size_t i=0; i<variable_indices.size(); ++i)
//				std::cout << variable_indices[i] << std::endl;

			// add constraint
			rval = QSadd_row(problem, (int) variable_indices.size(), &variable_indices[0], &variable_coefficients[0], 0.0, 'E', (const char *) NULL);

			if(rval)
				std::cout << "!!!!! failed to add constraint !!!!!" << std::endl;
		}
	}

	// if no weights are given an integer linear program should be solved, so the problem needs to be changed to this
	// by saving it to a file and reloading it (no better way available from Qsopt)
	if(W == NULL)
	{
		// save problem
		QSwrite_prob(problem, "lin_flow_prog.lp", "LP");

		// read in the original problem, before "End" include the definition of the variables as integers
		std::ifstream original_problem;
		original_problem.open("lin_flow_prog.lp", std::ifstream::in);
		std::ofstream new_problem;
		new_problem.open("int_lin_flow_prog.lp", std::ofstream::out);
		std::string interception_line = "End";
		std::string line;
		while (getline(original_problem,line))
		{
			if (line != interception_line)
			{
				new_problem << line << std::endl;
			}
			else
			{
				// include Integer section
				new_problem << "Integer" << std::endl;
				for(size_t variable=1; variable<=C.size(); ++variable)
				{
					new_problem << " x" << variable;

					// new line for reading convenience after 5 variables
					if(variable%5 == 0 && variable != C.size()-1)
					{
						new_problem << std::endl;
					}
				}

				// add "End" to the file to show end of it
				new_problem << std::endl << std::left << line << std::endl;
			}
		}
		original_problem.close();
		new_problem.close();

		// reload the problem
		problem = QSread_prob("int_lin_flow_prog.lp", "LP");
		if(problem == (QSprob) NULL)
		{
		    fprintf(stderr, "Unable to read and load the LP\n");
		}
	}

//	testing
	QSwrite_prob(problem, "lin_flow_prog.lp", "LP");

	// solve the optimization problem
	int status=0;
	QSget_intcount(problem, &status);
	std::cout << "number of integer variables in the problem: " << status << std::endl;
	rval = QSopt_primal(problem, &status);

	if (rval)
	{
	    fprintf (stderr, "QSopt_dual failed with return code %d\n", rval);
	}
	else
	{
	    switch (status)
	    {
	    	case QS_LP_OPTIMAL:
	    		printf ("Found optimal solution to LP\n");
	    		break;
	    	case QS_LP_INFEASIBLE:
	    		printf ("No feasible solution exists for the LP\n");
	    		break;
	    	case QS_LP_UNBOUNDED:
	    		printf ("The LP objective is unbounded\n");
	    		break;
	    	default:
	    		printf ("LP could not be solved, status = %d\n", status);
	    		break;
	    }
	}

	// retrieve solution
	double* result;
	result  = (double *) malloc(number_of_variables * sizeof (double));
	QSget_solution(problem, NULL, result, NULL, NULL, NULL);
	for(size_t variable=0; variable<number_of_variables; ++variable)
	{
		C[variable] = result[variable];
//		std::cout << result[variable] << std::endl;
	}

//	testing
	QSwrite_prob(problem, "lin_flow_prog.lp", "LP");

	// free space used by the optimization problem
	QSfree(problem);
}

// Function that creates a Qsopt optimization problem and solves it, using the given matrices and vectors and the two-stage
// ansatz, that takes an initial step going from the start node and then a coverage stage assuming that the number of
// flows into and out of a node must be the same. At last a final stage is gone, that terminates the path in one of the
// possible nodes.
template<typename T>
void flowNetworkExplorator::solveThreeStageOptimizationProblem(std::vector<T>& C, const cv::Mat& V, const std::vector<double>& weights,
			const std::vector<std::vector<uint> >& flows_into_nodes, const std::vector<std::vector<uint> >& flows_out_of_nodes,
			const std::vector<uint>& start_arcs, const std::vector<double>* W)
{
	// initialize the problem
	QSprob problem;
	problem = QScreate_prob("flowNetworkExploration", QS_MIN);

	std::cout << "Creating and solving linear program." << std::endl;

	// add the optimization variables to the problem
	int rval;
	for(size_t arc=0; arc<start_arcs.size(); ++arc) // initial stage
	{
		if(W != NULL) // if a relaxation-vector is provided, use it to set the weights for the variables
			rval = QSnew_col(problem, W->operator[](arc)*weights[start_arcs[arc]], 0.0, 1.0, (const char *) NULL);
		else
			rval = QSnew_col(problem, weights[start_arcs[arc]], 0.0, 1.0, (const char *) NULL);

		if(rval)
			std::cout << "!!!!! failed to add initial variable !!!!!" << std::endl;
	}
	for(size_t variable=0; variable<V.cols; ++variable) // coverage stage
	{
		if(W != NULL) // if a weight-vector is provided, use it to set the weights for the variables
			rval = QSnew_col(problem, W->operator[](variable + start_arcs.size())*weights[variable], 0.0, 1.0, (const char *) NULL);
		else
			rval = QSnew_col(problem, weights[variable], 0.0, 1.0, (const char *) NULL);

		if(rval)
			std::cout << "!!!!! failed to add coverage variable !!!!!" << std::endl;
	}
	int number_of_final_arcs = 0;
	for(size_t node=0; node<flows_out_of_nodes.size(); ++node) // final stage
	{
		for(size_t flow=0; flow<flows_out_of_nodes[node].size(); ++flow)
		{
			if(W != NULL) // if a weight-vector is provided, use it to set the weights for the variables
				rval = QSnew_col(problem, W->operator[](number_of_final_arcs + start_arcs.size() + V.cols)*weights[flows_out_of_nodes[node][flow]], 0.0, 1.0, (const char *) NULL);
			else
				rval = QSnew_col(problem, weights[flows_out_of_nodes[node][flow]], 0.0, 1.0, (const char *) NULL);

			if(rval)
				std::cout << "!!!!! failed to add final node !!!!!" << std::endl;
			else
				++number_of_final_arcs; // increase number of done flows out of nodes to access right optimization variable
		}
	}

	int number_of_variables = QSget_colcount(problem);
	std::cout << "number of variables in the problem: " << number_of_variables << std::endl;

	// inequality constraints to ensure that every position has been seen at least once:
	//		for each center that should be covered, find the arcs of the three stages that cover it
	for(size_t row=0; row<V.rows; ++row)
	{
		std::vector<int> variable_indices;

		// initial stage, TODO: check this for correctness
		for(size_t col=0; col<start_arcs.size(); ++col)
			if(V.at<uchar>(row, start_arcs[col])==1)
				variable_indices.push_back((int) col);

		// coverage stage
		for(size_t col=0; col<V.cols; ++col)
			if(V.at<uchar>(row, col)==1)
				variable_indices.push_back((int) col + start_arcs.size());

		// final stage
		int flow_counter = 0;
		for(size_t node=0; node<flows_out_of_nodes.size(); ++node)
		{
			for(size_t flow=0; flow<flows_out_of_nodes[node].size(); ++flow)
			{
				if(V.at<uchar>(row, flows_out_of_nodes[node][flow])==1)
				{
					variable_indices.push_back(flow_counter + start_arcs.size() + V.cols);
				}
				// increase number of done flows out of nodes to access right optimization variable
				++flow_counter;
			}
		}

		// all indices are 1 in this constraint
		std::vector<double> variable_coefficients(variable_indices.size(), 1.0);

		// add the constraint, if the current cell can be covered by the given arcs
		if(variable_indices.size()>0)
			rval = QSadd_row(problem, (int) variable_indices.size(), &variable_indices[0], &variable_coefficients[0], 1.0, 'G', (const char *) NULL);

		if(rval)
			std::cout << "!!!!! failed to add constraint !!!!!" << std::endl;
	}


	// equality constraint to ensure that the number of flows out of one node is the same as the number of flows into the
	// node during the coverage stage
	//	Remark: for initial stage ensure that exactly one arc is gone, because there only the outgoing flows are taken
	//			into account
	// initial stage
	std::vector<int> start_indices(start_arcs.size());
	std::vector<double> start_coefficients(start_arcs.size());
	for(size_t start=0; start<start_arcs.size(); ++start)
	{
		start_indices[start] = start;
		start_coefficients[start] = 1.0;
	}
	rval = QSadd_row(problem, (int) start_indices.size(), &start_indices[0], &start_coefficients[0], 1.0, 'E', (const char *) NULL);

	if(rval)
		std::cout << "!!!!! failed to add initial constraint !!!!!" << std::endl;

	// coverage stage
	for(size_t node=0; node<flows_into_nodes.size(); ++node)
	{
		std::vector<int> variable_indices;
		std::vector<double> variable_coefficients;

		// gather flows into node
		for(size_t inflow=0; inflow<flows_into_nodes[node].size(); ++inflow)
		{
			// if a start arcs flows into the node, additionally take the index of the arc in the start_arc vector
			if(contains(start_arcs, flows_into_nodes[node][inflow])==true)
			{
				variable_indices.push_back(std::find(start_arcs.begin(), start_arcs.end(), flows_into_nodes[node][inflow])-start_arcs.begin());
				variable_coefficients.push_back(1.0);
			}
			// get the index of the arc in the optimization vector
			variable_indices.push_back(flows_into_nodes[node][inflow] + start_arcs.size());
			variable_coefficients.push_back(1.0);
		}

		// gather flows out of node, also include flows into final nodes
		for(size_t outflow=0; outflow<flows_out_of_nodes[node].size(); ++outflow)
		{
			variable_indices.push_back(flows_out_of_nodes[node][outflow] + start_arcs.size());
			variable_coefficients.push_back(-1.0);
			variable_indices.push_back(flows_out_of_nodes[node][outflow] + start_arcs.size() + V.cols);
			variable_coefficients.push_back(-1.0);
		}

//		testing
//		std::cout << "number of flows: " << variable_indices.size() << std::endl;
//		for(size_t i=0; i<variable_indices.size(); ++i)
//			std::cout << variable_indices[i] << std::endl;

		// add constraint
		rval = QSadd_row(problem, (int) variable_indices.size(), &variable_indices[0], &variable_coefficients[0], 0.0, 'E', (const char *) NULL);

		if(rval)
			std::cout << "!!!!! failed to add constraint !!!!!" << std::endl;
	}

	// equality constraint to ensure that the path only once goes to the final stage
	std::vector<int> final_indices(number_of_final_arcs);
	std::vector<double> final_coefficients(number_of_final_arcs);
	// gather indices
	for(size_t node=0; node<number_of_final_arcs; ++node)
	{
		final_indices[node] = node + start_arcs.size() + V.cols;
		final_coefficients[node] = 1.0;
	}
	// add constraint
	rval = QSadd_row(problem, (int) final_indices.size(), &final_indices[0], &final_coefficients[0], 1.0, 'E', (const char *) NULL);

	if(rval)
		std::cout << "!!!!! failed to add constraint !!!!!" << std::endl;

	// if no weights are given an integer linear program should be solved, so the problem needs to be changed to this
	// by saving it to a file and reloading it (no better way available from Qsopt)
	if(W == NULL)
	{
		// save problem
		QSwrite_prob(problem, "lin_flow_prog.lp", "LP");

		// read in the original problem, before "End" include the definition of the variables as integers
		std::ifstream original_problem;
		original_problem.open("lin_flow_prog.lp", std::ifstream::in);
		std::ofstream new_problem;
		new_problem.open("int_lin_flow_prog.lp", std::ofstream::out);
		std::string interception_line = "End";
		std::string line;
		while (getline(original_problem,line))
		{
			if (line != interception_line)
			{
				new_problem << line << std::endl;
			}
			else
			{
				// include Integer section
				new_problem << "Integer" << std::endl;
				for(size_t variable=1; variable<=C.size(); ++variable)
				{
					new_problem << " x" << variable;

					// new line for reading convenience after 5 variables
					if(variable%5 == 0 && variable != C.size()-1)
					{
						new_problem << std::endl;
					}
				}

				// add "End" to the file to show end of it
				new_problem << std::endl << std::left << line << std::endl;
			}
		}
		original_problem.close();
		new_problem.close();

		// reload the problem
		problem = QSread_prob("int_lin_flow_prog.lp", "LP");
		if(problem == (QSprob) NULL)
		{
		    fprintf(stderr, "Unable to read and load the LP\n");
		}
	}

//	testing
	QSwrite_prob(problem, "lin_flow_prog.lp", "LP");

	// solve the optimization problem
	int status=0;
	QSget_intcount(problem, &status);
	std::cout << "number of integer variables in the problem: " << status << std::endl;
	rval = QSopt_dual(problem, &status);

	if (rval)
	{
	    fprintf (stderr, "QSopt_dual failed with return code %d\n", rval);
	}
	else
	{
	    switch (status)
	    {
	    	case QS_LP_OPTIMAL:
	    		printf ("Found optimal solution to LP\n");
	    		break;
	    	case QS_LP_INFEASIBLE:
	    		printf ("No feasible solution exists for the LP\n");
	    		break;
	    	case QS_LP_UNBOUNDED:
	    		printf ("The LP objective is unbounded\n");
	    		break;
	    	default:
	    		printf ("LP could not be solved, status = %d\n", status);
	    		break;
	    }
	}

	// retrieve solution
	double* result;
	result  = (double *) malloc(number_of_variables * sizeof (double));
	QSget_solution(problem, NULL, result, NULL, NULL, NULL);
	for(size_t variable=0; variable<number_of_variables; ++variable)
	{
		C[variable] = result[variable];
//		std::cout << result[variable] << std::endl;
	}

//	testing
	QSwrite_prob(problem, "lin_flow_prog.lp", "LP");

	// free space used by the optimization problem
	QSfree(problem);
}

// This Function checks if the given cv::Point is close enough to one cv::Point in the given vector. If one point gets found
// that this Point is nearer than the defined min_distance the function returns false to stop it immediately.
bool flowNetworkExplorator::pointClose(const std::vector<cv::Point>& points, const cv::Point& point, const double min_distance)
{
	double square_distance = min_distance * min_distance;
	for(std::vector<cv::Point>::const_iterator current_point = points.begin(); current_point != points.end(); ++current_point)
	{
		double dx = current_point->x - point.x;
		double dy = current_point->y - point.y;
		if( ((dx*dx + dy*dy)) <= square_distance)
			return true;
	}
	return false;
}

// Function that uses the flow network based method to determine a coverage path. To do so the following steps are done
//	I.	Discretize the free space into cells that have to be visited a least once by using the sampling distance given to
//		the function. Also create a flow network by sweeping a line along the y-/x-axis and creating an edge, whenever it
//		hits an obstacle. From this hit point go back along the sweep line until the distance is equal to the coverage
//		radius, because the free space should represent the area that should be totally covered. If in both directions
//		along the sweep line no point in the free space can be found, ignore it.
//	II.	Create the matrices and vectors for the optimization problem:
//		1. The weight vector w, storing the distances between edges.
//		2. The coverage matrix V, storing which cell can be covered when going along the arcs.
//			remark: A cell counts as covered, when its center is in the coverage radius around an arc.
//		3. The sets of arcs for each node, that store the incoming and outgoing arcs
// III.	Create a<nd solve the optimization problems in the following order:
//		1.	Find the start node that is closest to the given starting position. This start node is used as initial step
//			in the optimization problem.
//		2. 	Iteratively solve the weighted optimization problem to approximate the problem by a convex optimization. This
//			speeds up the solution and is done until the sparsity of the optimization variables doesn't change anymore,
//			i.e. converged, or a specific number of iterations is reached. To measure the sparsity a l^0_eps measure is
//			used, that checks |{i: c[i] <= eps}|. In each step the weights are adapted with respect to the previous solution.
//		3.	Solve the final optimization problem by discarding the arcs that correspond to zero elements in the previous
//			determined solution. This reduces the dimensionality of the problem and allows the algorithm to faster find
//			a solution.
void flowNetworkExplorator::getExplorationPath(const cv::Mat& room_map, std::vector<geometry_msgs::Pose2D>& path,
		const float map_resolution, const cv::Point starting_position, const cv::Point2d map_origin,
		const int cell_size, const geometry_msgs::Polygon& room_min_max_coordinates,
		const Eigen::Matrix<float, 2, 1>& robot_to_fow_middlepoint_vector, const float coverage_radius,
		const bool plan_for_footprint, const int sparsity_check_range)
{
	// *********** I. Discretize the free space and create the flow network ***********
	// find cell centers that need to be covered
	std::vector<cv::Point> cell_centers;
	for(size_t y=room_min_max_coordinates.points[0].y+0.5*cell_size; y<=room_min_max_coordinates.points[1].y; y+=cell_size)
		for(size_t x=room_min_max_coordinates.points[0].x+0.5*cell_size; x<=room_min_max_coordinates.points[1].x; x+=cell_size)
			if(room_map.at<uchar>(y,x)==255)
				cell_centers.push_back(cv::Point(x,y));

	// find edges for the flow network, sweeping along the y-axis
	std::vector<cv::Point> edges;
	int coverage_int = (int) std::floor(coverage_radius);
	std::cout << "y sweeping, radius: " << coverage_int << std::endl;
	for(size_t y=room_min_max_coordinates.points[0].y+coverage_int; y<=room_min_max_coordinates.points[1].y; ++y)
	{
//		cv::Mat test_map = room_map.clone();
		for(size_t x=0; x<room_map.cols; ++x)
		{
			// check if an obstacle has been found, only check outer parts of the occupied space
			if(room_map.at<uchar>(y,x)==0 && (room_map.at<uchar>(y-1,x)==255 || room_map.at<uchar>(y+1,x)==255))
			{
//				cv::circle(test_map, cv::Point(x,y), 2, cv::Scalar(127), CV_FILLED);
				// check on both sides along the sweep line if a free point is available, don't exceed matrix dimensions
				if(room_map.at<uchar>(y-coverage_int, x)==255 && y-coverage_int>=0)
					edges.push_back(cv::Point(x, y-coverage_int));
				else if(room_map.at<uchar>(y+coverage_int, x)==255 && y+coverage_int<room_map.rows)
					edges.push_back(cv::Point(x, y+coverage_int));

				// increase x according to the coverage radius, -1 because it gets increased after this for step
				x += 2.0*coverage_int-1;
			}
		}
//		cv::imshow("test", test_map);
//		cv::waitKey();
	}

	// sweep along x-axis
//	std::cout << "x sweeping" << std::endl;
//	for(size_t x=room_min_max_coordinates.points[0].x+coverage_int; x<=room_min_max_coordinates.points[1].x; ++x)
//	{
////		cv::Mat test_map = room_map.clone();
//		for(size_t y=0; y<room_map.rows; ++y)
//		{
//			// check if an obstacle has been found, only check outer parts of the occupied space
//			if(room_map.at<uchar>(y,x)==0 && (room_map.at<uchar>(y,x-1)==255 || room_map.at<uchar>(y,x+1)==255))
//			{
////				cv::circle(test_map, cv::Point(x,y), 2, cv::Scalar(127), CV_FILLED);
//				// check on both sides along the sweep line if a free point is available, don't exceed matrix dimensions
//				if(room_map.at<uchar>(y, x-coverage_int)==255 && x-coverage_int>=0)
//					edges.push_back(cv::Point(x-coverage_int, y));
//				else if(room_map.at<uchar>(y, x+coverage_int)==255 && x+coverage_int<room_map.cols)
//					edges.push_back(cv::Point(x+coverage_int, y));
//
//				// increase y according to the coverage radius, -1 because it gets increased after this for step
//				y += 2.0*coverage_int-1;
//			}
//		}
////		cv::imshow("test", test_map);
////		cv::waitKey();
//	}
	std::cout << "found " << edges.size() << " edges" << std::endl;

	// create the arcs for the flow network
	// TODO: reduce dimensionality, maybe only arcs that are straight (close enough to straight line)?
	std::cout << "Constructing distance matrix" << std::endl;
	cv::Mat distance_matrix; // determine weights
	DistanceMatrix::constructDistanceMatrix(distance_matrix, room_map, edges, 0.25, 0.0, map_resolution, path_planner_);
	std::cout << "Constructed distance matrix, defining arcs" << std::endl;
	std::vector<arcStruct> arcs;
	double max_distance = room_min_max_coordinates.points[1].y - room_min_max_coordinates.points[0].y; // arcs should at least go the maximal room distance to allow straight arcs
	for(size_t start=0; start<distance_matrix.rows; ++start)
	{
		for(size_t end=0; end<distance_matrix.cols; ++end)
		{
			// don't add arc from node to itself, only consider upper triangle of the distance matrix, one path from edge
			// to edge provides both arcs
			if(start!=end && end>start)
			{
				arcStruct current_forward_arc;
				current_forward_arc.start_point = edges[start];
				current_forward_arc.end_point = edges[end];
				current_forward_arc.weight = distance_matrix.at<double>(start, end);
				arcStruct current_backward_arc;
				current_backward_arc.start_point = edges[end];
				current_backward_arc.end_point = edges[start];
				current_forward_arc.weight = distance_matrix.at<double>(end, start);
				cv::Point vector = current_forward_arc.start_point - current_forward_arc.end_point;
				// don't add too long arcs to reduce dimensionality, because they certainly won't get chosen anyway
				// also don't add arcs that are too far away from the straight line (start-end) because they are likely
				// to go completely around obstacles and are not good
				if(current_forward_arc.weight <= max_distance && current_forward_arc.weight <= 1.1*cv::norm(vector)) // TODO: param
				{
					std::vector<cv::Point> astar_path;
					path_planner_.planPath(room_map, current_forward_arc.start_point, current_forward_arc.end_point, 1.0, 0.0, map_resolution, 0, &astar_path);
					current_forward_arc.edge_points = astar_path;
					// reverse path for backward arc
					std::reverse(astar_path.begin(), astar_path.end());
					current_backward_arc.edge_points = astar_path;
					arcs.push_back(current_forward_arc);
					arcs.push_back(current_backward_arc);
				}
			}
		}
	}
	std::cout << "arcs: " << arcs.size() << std::endl;

	// *********** II. Construct the matrices for the optimization problem ***********
	std::cout << "Starting to construct the matrices for the optimization problem." << std::endl;
	// 1. weight vector
	int number_of_candidates = arcs.size();
	std::vector<double> w(number_of_candidates);
	for(std::vector<arcStruct>::iterator arc=arcs.begin(); arc!=arcs.end(); ++arc)
		w[arc-arcs.begin()] = arc->weight;

	// 2. visibility matrix, storing which call can be covered when going along the arc
	//		remark: a cell counts as covered, when the center of each cell is in the coverage radius around the arc
	cv::Mat V = cv::Mat(cell_centers.size(), number_of_candidates, CV_8U); // binary variables
	for(std::vector<arcStruct>::iterator arc=arcs.begin(); arc!=arcs.end(); ++arc)
	{
		// use the pointClose function to check if a cell can be covered along the path
		for(std::vector<cv::Point>::iterator cell=cell_centers.begin(); cell!=cell_centers.end(); ++cell)
		{
			if(pointClose(arc->edge_points, *cell, 1.1*coverage_radius) == true)
				V.at<uchar>(cell-cell_centers.begin(), arc-arcs.begin()) = 1;
			else
				V.at<uchar>(cell-cell_centers.begin(), arc-arcs.begin()) = 0;
		}
	}

	// 3. set of arcs (indices) that are going into and out of one node
	std::vector<std::vector<uint> > flows_into_nodes(edges.size());
	std::vector<std::vector<uint> > flows_out_of_nodes(edges.size());
	int number_of_outflows = 0;
	for(std::vector<cv::Point>::iterator edge=edges.begin(); edge!=edges.end(); ++edge)
	{
		for(std::vector<arcStruct>::iterator arc=arcs.begin(); arc!=arcs.end(); ++arc)
		{
			// if the start point of the arc is the edge save it as incoming flow
			if(arc->start_point == *edge)
			{
				flows_into_nodes[edge-edges.begin()].push_back(arc-arcs.begin());
				++number_of_outflows;
			}
			// if the end point of the arc is the edge save it as outgoing flow
			else if(arc->end_point == *edge)
				flows_out_of_nodes[edge-edges.begin()].push_back(arc-arcs.begin());
		}
	}

//	testing
//	for(size_t i=0; i<flows_into_nodes.size(); ++i)
//	{
//		std::cout << "in: " << std::endl;
//		for(size_t j=0; j<flows_into_nodes[i].size(); ++j)
//			std::cout << flows_into_nodes[i][j] << std::endl;
//		std::cout << "out: " << std::endl;
//		for(size_t j=0; j<flows_out_of_nodes[i].size(); ++j)
//			std::cout << flows_out_of_nodes[i][j] << std::endl;
//		std::cout << std::endl;
//	}

	std::cout << "Constructed all matrices for the optimization problem. Checking if all cells can be covered." << std::endl;

	// print out warning if a defined cell is not coverable with the chosen arcs
	bool all_cells_covered = true;
	for(size_t row=0; row<V.rows; ++row)
	{
		int number_of_paths = 0;
		for(size_t col=0; col<V.cols; ++col)
			if(V.at<uchar>(row, col)==1)
				++number_of_paths;
		if(number_of_paths==0)
		{
			std::cout << "!!!!!!!! EMPTY ROW OF VISIBILITY MATRIX !!!!!!!!!!!!!" << std::endl << "cell " << row << " not coverable" << std::endl;
			all_cells_covered = false;
		}
	}
	if(all_cells_covered == false)
		std::cout << "!!!!! WARNING: Not all cells could be covered with the given parameters, try changing them or ignore it to not cover the whole free space." << std::endl;

	// *********** III. Solve the different optimization problems ***********
	// 1. Find the start node closest to the starting position.
	double min_distance = 1e5;
	uint start_index = 0;
	for(std::vector<cv::Point>::iterator edge=edges.begin(); edge!=edges.end(); ++edge)
	{
		cv::Point difference_vector = *edge - starting_position;
		double current_distance = cv::norm(difference_vector);
		if(current_distance<min_distance)
		{
			min_distance = current_distance;
			start_index = edge-edges.begin();
		}
	}

	// 2. iteratively solve the optimization problem, using convex relaxation
	std::vector<double> C_small(flows_out_of_nodes[start_index].size()+number_of_candidates+number_of_outflows);
	std::vector<double> W_small(C_small.size(), 1.0);
	std::cout << "number of outgoing arcs: " << number_of_outflows << std::endl;
//	solveThreeStageOptimizationProblem(C_small, V, w, flows_into_nodes, flows_out_of_nodes, flows_out_of_nodes[0], &W_small);

	// ************** multi stage *******************
	int number_of_stages = edges.size()/4;
	std::vector<double> C(flows_out_of_nodes[start_index].size()+number_of_candidates*(number_of_stages-1));
	std::vector<double> W(C.size(), 1.0);
	std::cout << "start arcs number: " << flows_out_of_nodes[start_index].size() << ", initial stages: " << number_of_stages << std::endl;
//	solveMultiStageOptimizationProblem(C, V, w, flows_into_nodes, flows_out_of_nodes, number_of_stages, flows_out_of_nodes[0], &W);
	// ***********************************************

	bool sparsity_converged = false; // boolean to check, if the sparsity of C has converged to a certain value
	double weight_epsilon = 0.0; // parameter that is used to update the weights after one solution has been obtained
	uint number_of_iterations = 0;
	std::vector<uint> sparsity_measures; // vector that stores the computed sparsity measures to check convergence
	double euler_constant = std::exp(1.0);
	do
	{
		// increase number of iterations
		++number_of_iterations;

		// solve optimization of the current step
//		solveMultiStageOptimizationProblem(C, V, w, flows_into_nodes, flows_out_of_nodes, number_of_stages, flows_out_of_nodes[start_index], &W);
		solveThreeStageOptimizationProblem(C_small, V, w, flows_into_nodes, flows_out_of_nodes, flows_out_of_nodes[start_index], &W_small);

		// update epsilon and W
		int exponent = 1 + (number_of_iterations - 1)*0.1;
		weight_epsilon = std::pow(1/(euler_constant-1), exponent);
		for(size_t weight=0; weight<W_small.size(); ++weight)
			W_small[weight] = weight_epsilon/(weight_epsilon + C_small[weight]);

		// measure sparsity of C to check terminal condition, used measure: l^0_eps (|{i: c[i] <= eps}|)
		uint sparsity_measure = 0;
		for(size_t variable=0; variable<C_small.size(); ++variable)
			if(C_small[variable]<=0.01)
				++sparsity_measure;
		sparsity_measures.push_back(sparsity_measure);

		// check terminal condition, i.e. if the sparsity hasn't improved in the last n steps using l^0_eps measure,
		// if enough iterations have been done yet
		if(sparsity_measures.size() >= sparsity_check_range)
		{
			uint number_of_last_measure = 0;
			for(std::vector<uint>::reverse_iterator measure=sparsity_measures.rbegin(); measure!=sparsity_measures.rbegin()+sparsity_check_range && measure!=sparsity_measures.rend(); ++measure)
				if(*measure >= sparsity_measures.back())
					++number_of_last_measure;

			if(number_of_last_measure == sparsity_check_range)
				sparsity_converged = true;
		}

		std::cout << "Iteration: " << number_of_iterations << ", sparsity: " << sparsity_measures.back() << std::endl;
	}while(sparsity_converged == false && number_of_iterations <= 50); // TODO: param

	// 3. discard the arcs corresponding to zero elements in the optimization vector and solve the final optimization
	//	  problem
	cv::Mat test_map = room_map.clone();
	std::set<uint> used_arcs; // set that stores the indices of the arcs corresponding to non-zero elements in the solution
	// go trough the start arcs and determine the new start arcs
	for(size_t start_arc=0; start_arc<flows_out_of_nodes[start_index].size(); ++start_arc)
	{
		if(C_small[start_arc]!=0)
		{
			// insert start index
			used_arcs.insert(flows_out_of_nodes[start_index][start_arc]);

			std::vector<cv::Point> path=arcs[flows_out_of_nodes[start_index][start_arc]].edge_points;
			for(size_t j=0; j<path.size(); ++j)
				test_map.at<uchar>(path[j])=100;

			cv::imshow("discretized", test_map);
			cv::waitKey();
		}
	}

	// go trough the coverage stage
	for(size_t arc=flows_out_of_nodes[start_index].size(); arc<flows_out_of_nodes[start_index].size()+arcs.size(); ++arc)
	{
		if(C_small[arc]!=0)
		{
			// insert index, relative to the first coverage variable
			used_arcs.insert(arc-flows_out_of_nodes[start_index].size());

			std::vector<cv::Point> path=arcs[arc-flows_out_of_nodes[start_index].size()].edge_points;
			for(size_t j=0; j<path.size(); ++j)
				test_map.at<uchar>(path[j])=100;

			cv::imshow("discretized", test_map);
			cv::waitKey();
		}
	}

	// go trough the final stage and find the remaining used arcs
	std::vector<std::vector<uint> > reduced_outflows(flows_out_of_nodes.size());
	uint flow_counter = 0;
	for(size_t node=0; node<flows_out_of_nodes.size(); ++node)
	{
		for(size_t flow=0; flow<flows_out_of_nodes[node].size(); ++flow)
		{
			if(C_small[flow_counter+flows_out_of_nodes[start_index].size()+flows_out_of_nodes.size()]!=0)
			{
				// insert saved outgoing flow index
				used_arcs.insert(flows_out_of_nodes[node][flow]);
				std::vector<cv::Point> path=arcs[flows_out_of_nodes[node][flow]].edge_points;
				for(size_t j=0; j<path.size(); ++j)
					test_map.at<uchar>(path[j])=100;

				cv::imshow("discretized", test_map);
				cv::waitKey();
			}
		}
	}

	std::cout << "got " << used_arcs.size() << " used arcs" << std::endl;

	// go trough the indices of the used arcs and save the arcs as new candidates, also determine the nodes that correspond
	// to these arcs (i.e. are either start or end)
	std::vector<arcStruct> reduced_arc_candidates;
	std::vector<cv::Point> reduced_edges;
	for(std::set<uint>::iterator candidate=used_arcs.begin(); candidate!=used_arcs.end(); ++candidate)
	{
		arcStruct current_arc = arcs[*candidate];
		cv::Point start = current_arc.start_point;
		cv::Point end = current_arc.end_point;
		reduced_arc_candidates.push_back(current_arc);

		// if the start/end hasn't been already saved, save it
		if(contains(reduced_edges, start)==false)
			reduced_edges.push_back(start);
		if(contains(reduced_edges, end)==false)
			reduced_edges.push_back(end);
	}

	// determine the reduced outgoing and incoming flows and find new start index
	std::vector<std::vector<uint> > reduced_flows_into_nodes(reduced_edges.size());
	std::vector<std::vector<uint> > reduced_flows_out_of_nodes(reduced_edges.size());
	uint reduced_start_index = 0;
	for(std::vector<cv::Point>::iterator edge=reduced_edges.begin(); edge!=reduced_edges.end(); ++edge)
	{
		for(std::vector<arcStruct>::iterator arc=reduced_arc_candidates.begin(); arc!=reduced_arc_candidates.end(); ++arc)
		{
			// if the start point of the arc is the edge save it as incoming flow
			if(arc->start_point == *edge)
			{
				reduced_flows_into_nodes[edge-reduced_edges.begin()].push_back(arc-reduced_arc_candidates.begin());

				// check if current origin of the arc is determined start edge
				if(*edge == edges[start_index])
				{
					reduced_start_index = edge-reduced_edges.begin();
					std::cout << "found new start index" << std::endl;
				}
			}
			// if the end point of the arc is the edge save it as outgoing flow
			else if(arc->end_point == *edge)
				reduced_flows_out_of_nodes[edge-reduced_edges.begin()].push_back(arc-reduced_arc_candidates.begin());
		}
	}

	std::cout << "number of arcs (" << reduced_flows_out_of_nodes.size() << ") for the reduced edges:" << std::endl;
	for(size_t i=0; i<reduced_flows_out_of_nodes.size(); ++i)
		std::cout << "n" << (int) i << ": " << reduced_flows_out_of_nodes[i].size() << std::endl;

	// remove the first initial column
	uint new_number_of_variables = 0;
	cv::Mat V_reduced = cv::Mat(cell_centers.size(), 1, CV_8U); // initialize one column because opencv wants it this way, add other columns later
	for(std::set<uint>::iterator var=used_arcs.begin(); var!=used_arcs.end(); ++var)
	{
		// gather column corresponding to this candidate pose and add it to the new observability matrix
		cv::Mat column = V.col(*var);
		cv::hconcat(V_reduced, column, V_reduced);
	}
	V_reduced = V_reduced.colRange(1, V_reduced.cols);

	for(size_t row=0; row<V_reduced.rows; ++row)
	{
		int one_count = 0;
		for(size_t col=0; col<V_reduced.cols; ++col)
		{
			std::cout << (int) V_reduced.at<uchar>(row, col) << " ";
			if(V_reduced.at<uchar>(row, col)!=0)
				++one_count;
		}
		std::cout << std::endl;
		if(one_count == 0)
			std::cout << "!!!!!!!!!!!!! empty row !!!!!!!!!!!!!!!!!!" << std::endl;
	}

//	testing
//	cv::Mat test_map = room_map.clone();
//	for(size_t i=0; i<cell_centers.size(); ++i)
//		cv::circle(test_map, cell_centers[i], 2, cv::Scalar(75), CV_FILLED);
	for(size_t i=0; i<reduced_edges.size(); ++i)
		cv::circle(test_map, reduced_edges[i], 2, cv::Scalar(150), CV_FILLED);
//	for(size_t i=0; i<reduced_arc_candidates.size(); ++i)
//	{
//		std::vector<cv::Point> path=reduced_arc_candidates[i].edge_points;
//		for(size_t j=0; j<path.size(); ++j)
//			test_map.at<uchar>(path[j])=100;
//	}
	cv::imshow("discretized", test_map);
	cv::waitKey();
}
