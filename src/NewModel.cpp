/**
*
* \author     Monika Roznere <mroznere@gmail.com>
* \copyright  Copyright (c) 2019, Dartmouth Robotics Lab.
*
*/

#include "underwater_color_enhance/NewModel.h"

#include <math.h>
#include <opencv2/opencv.hpp>
#include <dlib/optimization.h>
#include <utility>
#include <tinyxml.h>
#include <string>
#include <vector>

namespace underwater_color_enhance
{

typedef dlib::matrix<double, 2, 1> input_vector;
typedef dlib::matrix<double, 2, 1> parameter_vector;


double model(const input_vector& input, const parameter_vector& params)
{
  const double backscatter_val = params(0);
  const double direct_signal_val = params(1);

  const double observed_color = input(0);
  const double wideband_veiling_light = input(1);

  const double correct_color = (observed_color - (wideband_veiling_light * backscatter_val)) / direct_signal_val;

  return correct_color;
}


double residual(const std::pair<input_vector, double>& data, const parameter_vector& params)
{
  return model(data.first, params) - data.second;
}


void NewModel::calculate_optimized_attenuation(cv::Mat& img)
{
    if (this->CHECK_TIME)
    {
      this->begin = clock();
    }

    // Split BGR image to a Mat array of each color channel
    cv::Mat bgr[3];
    split(img, bgr);

    if (this->CHECK_TIME)
    {
      this->end = clock();
      std::cout << "LOG: Set image for processing complete. Time: " <<
        static_cast<double>(this->end - this->begin) / CLOCKS_PER_SEC << std::endl;

      this->begin = clock();
    }
    else if (this->LOG_SCREEN)
    {
      std::cout << "LOG: Set image for processing complete" << std::endl;
    }

    // Calculate or estimate wideband veiling light
    cv::Scalar wideband_veiling_light;
    // FUTURE: average wideband veiling light be calculates using image processing techniques
    if (this->EST_VEILING_LIGHT)  // Estimate wideband veiling light as average background value
    {
      cv::Rect region_of_interest(this->scene->BACKGROUND_SAMPLE[0], this->scene->BACKGROUND_SAMPLE[1],
        this->scene->BACKGROUND_SAMPLE[2], this->scene->BACKGROUND_SAMPLE[3]);
      cv::Mat background = img(region_of_interest);

      // TO DO: this mean is done independently for each channel
      // Should I take the average pixel color instead?
      wideband_veiling_light = mean(background);
    }
    else
    {
      wideband_veiling_light = calc_wideband_veiling_light();
    }

    if (this->CHECK_TIME)
    {
      this->end = clock();
      std::cout << "LOG: Veiling light calculation complete. Time: " <<
        static_cast<double>(this->end - this->begin) / CLOCKS_PER_SEC << std::endl;

      this->begin = clock();
    }
    else if (this->LOG_SCREEN)
    {
      std::cout << "LOG: Veiling light calculation complete" << std::endl;
    }

    // TO DO: Could have these rectangles initialized ahead of time
    // Create rectangles of the regions of interest
    cv::Rect patch_1_region(this->scene->COLOR_1_SAMPLE[0], this->scene->COLOR_1_SAMPLE[1],
      this->scene->COLOR_1_SAMPLE[2], this->scene->COLOR_1_SAMPLE[3]);
    cv::Rect patch_2_region(this->scene->COLOR_2_SAMPLE[0], this->scene->COLOR_2_SAMPLE[1],
       this->scene->COLOR_2_SAMPLE[2], this->scene->COLOR_2_SAMPLE[3]);
    // Splice regions from the image
    cv::Mat color_1_region = img(patch_1_region);
    cv::Mat color_2_region = img(patch_2_region);

    // TO DO: this mean is done independently for each channel. Should I take the average pixel color instead?
    // mean pixel value of observed colors
    cv::Scalar color_1_obs = mean(color_1_region);
    cv::Scalar color_2_obs = mean(color_2_region);

    // Check if max depth range has been set
    if (this->depth_max_range == -1)
    {
      this->depth_max_range = fabs((this->depth + 0.5) * 2);
      this->depth_max_range = roundf(this->depth_max_range * 1) / 2;
    }

    if (this->depth < this->depth_max_range && this->depth > this->depth_max_range - this->RANGE)
    {
      // Blue channel observations

      this->observed_input(0) = static_cast<double>(color_1_obs[0]);
      this->observed_input(1) = static_cast<double>(wideband_veiling_light[0]);

      this->observed_samples_blue.push_back(std::make_pair(this->observed_input, this->COLOR_1_TRUTH[0]));

      this->observed_input(0) = static_cast<double>(color_2_obs[0]);

      this->observed_samples_blue.push_back(std::make_pair(this->observed_input, this->COLOR_2_TRUTH[0]));

      // Green channel observations

      this->observed_input(0) = static_cast<double>(color_1_obs[1]);
      this->observed_input(1) = static_cast<double>(wideband_veiling_light[1]);

      this->observed_samples_green.push_back(std::make_pair(this->observed_input, this->COLOR_1_TRUTH[1]));

      this->observed_input(0) = static_cast<double>(color_2_obs[1]);

      this->observed_samples_green.push_back(std::make_pair(this->observed_input, this->COLOR_2_TRUTH[1]));

      // Red channel observations

      this->observed_input(0) = static_cast<double>(color_1_obs[2]);
      this->observed_input(1) = static_cast<double>(wideband_veiling_light[2]);

      this->observed_samples_red.push_back(std::make_pair(this->observed_input, this->COLOR_1_TRUTH[2]));

      this->observed_input(0) = static_cast<double>(color_2_obs[2]);

      this->observed_samples_red.push_back(std::make_pair(this->observed_input, this->COLOR_2_TRUTH[2]));
    }
    else if (this->depth > this->depth_max_range)
    {
      parameter_vector optimized_att;

      // Blue channel optimization

      optimized_att = 1;
      dlib::solve_least_squares_lm(dlib::objective_delta_stop_strategy(1e-7).be_verbose(),
                                    residual,
                                    dlib::derivative(residual),
                                    this->observed_samples_blue,
                                    optimized_att);
      this->backscatter_att[0] = optimized_att(0);
      this->direct_signal_att[0] = optimized_att(1);
      std::cout << this->backscatter_att[0] << std::endl;

      // Green channel optimization

      optimized_att = 1;
      dlib::solve_least_squares_lm(dlib::objective_delta_stop_strategy(1e-7).be_verbose(),
                                    residual,
                                    dlib::derivative(residual),
                                    this->observed_samples_green,
                                    optimized_att);
      this->backscatter_att[1] = optimized_att(0);
      this->direct_signal_att[1] = optimized_att(1);


      // Red channel optimization

      optimized_att = 1;
      dlib::solve_least_squares_lm(dlib::objective_delta_stop_strategy(1e-7).be_verbose(),
                                    residual,
                                    dlib::derivative(residual),
                                    this->observed_samples_red,
                                    optimized_att);
      this->backscatter_att[2] = optimized_att(0);
      this->direct_signal_att[2] = optimized_att(1);

      if (this->SAVE_DATA)
      {
        std::cout << "saving data" << std::endl;
        // Add declaration to the top of the XML file
        if (!this->file_initialized)
        {
          // std::cout << "INITIALIZED FILE" << std::endl;
          initialize_file();
        }
        set_data_to_file();
      }

      // Reinitialize samples and depth range
      this->observed_samples_blue.clear();
      this->observed_samples_green.clear();
      this->observed_samples_red.clear();

      this->depth_max_range += this->RANGE;
    }
}


/** No SLAM implementation
 */
cv::Mat NewModel::color_correct(cv::Mat& img)
{
  if (this->CHECK_TIME)
  {
    this->begin = clock();
  }

  // Split BGR image to a Mat array of each color channel
  cv::Mat bgr[3];
  split(img, bgr);

  std::vector<cv::Mat> corrected_bgr(3);

  if (this->CHECK_TIME)
  {
    this->end = clock();
    std::cout << "LOG: Set image for processing complete. Time: " <<
      static_cast<double>(this->end - this->begin) / CLOCKS_PER_SEC << std::endl;

    this->begin = clock();
  }
  else if (this->LOG_SCREEN)
  {
    std::cout << "LOG: Set image for processing complete" << std::endl;
  }

  // Calculate or estimate wideband veiling light
  cv::Scalar wideband_veiling_light;
  // FUTURE: average wideband veiling light be calculates using image processing techniques
  if (this->EST_VEILING_LIGHT)  // Estimate wideband veiling light as average background value
  {
    cv::Rect region_of_interest(this->scene->BACKGROUND_SAMPLE[0], this->scene->BACKGROUND_SAMPLE[1],
      this->scene->BACKGROUND_SAMPLE[2], this->scene->BACKGROUND_SAMPLE[3]);
    cv::Mat background = img(region_of_interest);

    // TO DO: this mean is done independently for each channel
    // Should I take the average pixel color instead?
    wideband_veiling_light = mean(background);
  }
  else
  {
    wideband_veiling_light = calc_wideband_veiling_light();
  }

  if (this->CHECK_TIME)
  {
    this->end = clock();
    std::cout << "LOG: Veiling light calculation complete. Time: " <<
      static_cast<double>(this->end - this->begin) / CLOCKS_PER_SEC << std::endl;

    this->begin = clock();
  }
  else if (this->LOG_SCREEN)
  {
    std::cout << "LOG: Veiling light calculation complete" << std::endl;
  }

  if (this->PRIOR_DATA)  // Use prior data to retrieve backscatter and direct signal attenauation values
  {
    est_attenuation();
  }
  else  // Must calculate the attenuation values using a color chart
  {
    // TO DO: Could have these rectangles initialized ahead of time
    // Create rectangles of the regions of interest
    cv::Rect patch_1_region(this->scene->COLOR_1_SAMPLE[0], this->scene->COLOR_1_SAMPLE[1],
      this->scene->COLOR_1_SAMPLE[2], this->scene->COLOR_1_SAMPLE[3]);
    cv::Rect patch_2_region(this->scene->COLOR_2_SAMPLE[0], this->scene->COLOR_2_SAMPLE[1],
       this->scene->COLOR_2_SAMPLE[2], this->scene->COLOR_2_SAMPLE[3]);
    // Splice regions from the image
    cv::Mat color_1_region = img(patch_1_region);
    cv::Mat color_2_region = img(patch_2_region);
// calc_attenuation(color_1_obs, color_2_obs, wideband_veiling_light);
    // TO DO: this mean is done independently for each channel. Should I take the average pixel color instead?
    // mean pixel value of observed colors
    cv::Scalar color_1_obs = mean(color_1_region);
    cv::Scalar color_2_obs = mean(color_2_region);

    calc_attenuation(color_1_obs, color_2_obs, wideband_veiling_light);
  }

  if (this->CHECK_TIME)
  {
    this->end = clock();
    std::cout << "LOG: Attenuation calculation complete. Time: " <<
      static_cast<double>(this->end - this->begin) / CLOCKS_PER_SEC << std::endl;

    this->begin = clock();
  }
  else if (this->LOG_SCREEN)
  {
    std::cout << "LOG: Attenuation calculation complete" << std::endl;
  }

  // Calculate backscatter and direct signal values
  float blue_backscatter_val = 1.0 - exp(-1.0 * this->backscatter_att[0] * this->scene->DISTANCE);
  float green_backscatter_val = 1.0 - exp(-1.0 * this->backscatter_att[1] * this->scene->DISTANCE);
  float red_backscatter_val = 1.0 - exp(-1.0 * this->backscatter_att[2] * this->scene->DISTANCE);

  float blue_direct_signal_val = exp(-1.0 * this->direct_signal_att[0] * this->scene->DISTANCE);
  float green_direct_signal_val = exp(-1.0 * this->direct_signal_att[1] * this->scene->DISTANCE);
  float red_direct_signal_val = exp(-1.0 * this->direct_signal_att[2] * this->scene->DISTANCE);

  // Implement color enhancement.
  corrected_bgr[0] = (bgr[0] - (wideband_veiling_light[0] * blue_backscatter_val)) / blue_direct_signal_val;
  corrected_bgr[1] = (bgr[1] - (wideband_veiling_light[1] * green_backscatter_val)) / green_direct_signal_val;
  corrected_bgr[2] = (bgr[2] - (wideband_veiling_light[2] * red_backscatter_val)) / red_direct_signal_val;

  if (this->CHECK_TIME)
  {
    this->end = clock();
    std::cout << "LOG: New method enhancment complete. Time: " <<
      static_cast<double>(this->end - this->begin) / CLOCKS_PER_SEC << std::endl;

    this->begin = clock();
  }
  else if (this->LOG_SCREEN)
  {
    std::cout << "LOG: New method enhancment complete" << std::endl;
  }

  // Merge the BGR channels into normal image format.
  cv::Mat corrected_img;
  merge(corrected_bgr, corrected_img);

  if (this->CHECK_TIME)
  {
    this->end = clock();
    std::cout << "LOG: Merge image complete. Time: " << double(this->end - this->begin) / CLOCKS_PER_SEC << std::endl;
  }
  else if (this->LOG_SCREEN)
  {
    std::cout << "LOG: Merge image complete." << std::endl;
  }

  if (this->SAVE_DATA)
  {
    // Add declaration to the top of the XML file
    if (!this->file_initialized)
    {
      initialize_file();
    }
    set_data_to_file();
  }

  return corrected_img;
}


/** SLAM implementation that utilizes feature points
 */
cv::Mat NewModel::color_correct_slam(cv::Mat& img, std::vector<cv::Point2f> point_data,
  std::vector<float> distance_data)
{
  if (this->CHECK_TIME)
  {
    this->begin = clock();
  }

  // Setting up for Voronoi Diagram algorithm
  cv::Rect rect(0, 0, img.cols, img.rows);
  cv::Subdiv2D subdiv(rect);

  for (int i = 0; i < distance_data.size(); i++)
  {
    subdiv.insert(point_data.at(i));
  }

  // Could initialize all of these ahead of time
  cv::Mat img_voronoi = cv::Mat::zeros(img.rows, img.cols, CV_32FC1);

  std::vector<std::vector<cv::Point2f> > facets;
  std::vector<cv::Point2f> centers;
  subdiv.getVoronoiFacetList(std::vector<int>(), facets, centers);

  std::vector<cv::Point> ifacet;
  for (size_t i = 0; i < facets.size(); i++)
  {
    ifacet.resize(facets[i].size());
    for (size_t j = 0; j < facets[i].size(); j++)
    {
      ifacet[j] = facets[i][j];
    }
    fillConvexPoly(img_voronoi, ifacet, cv::Scalar(distance_data[i]), 0, 0);
  }

  // Split BGR image to a Mat array of each color channel
  cv::Mat bgr[3];
  split(img, bgr);

  std::vector<cv::Mat> corrected_bgr(3);

  if (this->CHECK_TIME)
  {
    this->end = clock();
    std::cout << "LOG: Set image for processing complete. Time: " <<
      static_cast<double>(this->end - this->begin) / CLOCKS_PER_SEC << std::endl;

    this->begin = clock();
  }
  else if (this->LOG_SCREEN)
  {
    std::cout << "LOG: Set image for processing complete" << std::endl;
  }

  // Calculate or estimate wideband veiling light
  cv::Scalar wideband_veiling_light;
  // FUTURE: average wideband veiling light be calculates using image processing techniques
  if (this->EST_VEILING_LIGHT)  // Estimate wideband veiling light as average background value
  {
    cv::Rect region_of_interest(this->scene->BACKGROUND_SAMPLE[0], this->scene->BACKGROUND_SAMPLE[1],
      this->scene->BACKGROUND_SAMPLE[2], this->scene->BACKGROUND_SAMPLE[3]);
    cv::Mat background = img(region_of_interest);

    // TO DO: this mean is done independently for each channel
    // Should I take the average pixel color instead?
    wideband_veiling_light = mean(background);
    // cv::Scalar wideband_veiling_light = cv::Scalar(255, 255, 255);
  }
  else  // FUTURE: implement calculation for wideband veiling light
  {
    wideband_veiling_light = calc_wideband_veiling_light();
  }

  if (this->CHECK_TIME)
  {
    this->end = clock();
    std::cout << "LOG: Veiling light calculation complete. Time: " <<
      static_cast<double>(this->end - this->begin) / CLOCKS_PER_SEC << std::endl;

    this->begin = clock();
  }
  else if (this->LOG_SCREEN)
  {
    std::cout << "LOG: Veiling light calculation complete" << std::endl;
  }

  if (this->PRIOR_DATA)  // Use prior data to retrieve backscatter and direct signal attenauation values
  {
    est_attenuation();
  }
  else  // Must calculate the attenuation values using a color chart
  {
    // TO DO: Could have these rectangles initialized ahead of time
    // Create rectangles of the regions of interest
    cv::Rect patch_1_region(this->scene->COLOR_1_SAMPLE[0], this->scene->COLOR_1_SAMPLE[1],
      this->scene->COLOR_1_SAMPLE[2], this->scene->COLOR_1_SAMPLE[3]);
    cv::Rect patch_2_region(this->scene->COLOR_2_SAMPLE[0], this->scene->COLOR_2_SAMPLE[1],
       this->scene->COLOR_2_SAMPLE[2], this->scene->COLOR_2_SAMPLE[3]);
    // Splice regions from the image
    cv::Mat color_1_region = img(patch_1_region);
    cv::Mat color_2_region = img(patch_2_region);

    // TO DO: this mean is done independently for each channel. Should I take the average pixel color instead?
    // mean pixel value of observed colors
    cv::Scalar color_1_obs = mean(color_1_region);
    cv::Scalar color_2_obs = mean(color_2_region);

    calc_attenuation(color_1_obs, color_2_obs, wideband_veiling_light);
  }

  if (this->CHECK_TIME)
  {
    this->end = clock();
    std::cout << "LOG: Attenuation calculation complete. Time: " <<
      static_cast<double>(this->end - this->begin) / CLOCKS_PER_SEC << std::endl;

    this->begin = clock();
  }
  else if (this->LOG_SCREEN)
  {
    std::cout << "LOG: Attenuation calculation complete" << std::endl;
  }

  // Calculate backscatter and direct signal values
  cv::Mat blue_backscatter_val = -1.0 * this->backscatter_att[0] * img_voronoi;
  cv::exp(blue_backscatter_val, blue_backscatter_val);
  blue_backscatter_val = 1.0 - blue_backscatter_val;
  cv::Mat green_backscatter_val = -1.0 * this->backscatter_att[1] * img_voronoi;
  cv::exp(green_backscatter_val, green_backscatter_val);
  green_backscatter_val = 1.0 - green_backscatter_val;
  cv::Mat red_backscatter_val = -1.0 * this->backscatter_att[2] * img_voronoi;
  cv::exp(red_backscatter_val, red_backscatter_val);
  red_backscatter_val = 1.0 - red_backscatter_val;

  cv::Mat blue_direct_signal_val = -1.0 * this->direct_signal_att[0] * img_voronoi;
  cv::exp(blue_direct_signal_val, blue_direct_signal_val);
  cv::Mat green_direct_signal_val = -1.0 * this->direct_signal_att[1] * img_voronoi;
  cv::exp(green_direct_signal_val, green_direct_signal_val);
  cv::Mat red_direct_signal_val = -1.0 * this->direct_signal_att[2] * img_voronoi;
  cv::exp(red_direct_signal_val, red_direct_signal_val);

  // Implement color enhancement.
  corrected_bgr[0] = (bgr[0] - (wideband_veiling_light[0] * blue_backscatter_val)) / blue_direct_signal_val;
  corrected_bgr[1] = (bgr[1] - (wideband_veiling_light[1] * green_backscatter_val)) / green_direct_signal_val;
  corrected_bgr[2] = (bgr[2] - (wideband_veiling_light[2] * red_backscatter_val)) / red_direct_signal_val;

  if (this->CHECK_TIME)
  {
    this->end = clock();
    std::cout << "LOG: New method enhancment complete. Time: " <<
      static_cast<double>(this->end - this->begin) / CLOCKS_PER_SEC << std::endl;

    this->begin = clock();
  }
  else if (this->LOG_SCREEN)
  {
    std::cout << "LOG: New method enhancment complete" << std::endl;
  }

  // Merge the BGR channels into normal image format.
  cv::Mat corrected_img;
  merge(corrected_bgr, corrected_img);

  if (this->CHECK_TIME)
  {
    this->end = clock();
    std::cout << "LOG: Merge image complete. Time: " << double(this->end - this->begin) / CLOCKS_PER_SEC << std::endl;
  }
  else if (this->LOG_SCREEN)
  {
    std::cout << "LOG: Merge image complete." << std::endl;
  }

  if (this->SAVE_DATA)
  {
    // Add declaration to the top of the XML file
    if (!this->file_initialized)
    {
      initialize_file();
    }
    set_data_to_file();
  }

  return corrected_img;
}


/** Calculate background pixel using known characteristics of camera and underwater_scene
 */
cv::Scalar NewModel::calc_wideband_veiling_light()
{
  float temp_cur = this->scene->b_sca[0] * this->scene->irradiance[0] / this->scene->b_att[0];
  cv::Scalar wideband_veiling_light = this->scene->camera_response[0] * temp_cur;

  for (int i = 1; i < this->scene->WAVELENGTHS.size() - 1; i++)
  {
    temp_cur = this->scene->b_sca[i] * this->scene->irradiance[i] / this->scene->b_att[i];
    wideband_veiling_light +=  2.0 * this->scene->camera_response[i] * temp_cur;
  }

  temp_cur = this->scene->b_sca.back() * this->scene->irradiance.back() / this->scene->b_att.back();
  wideband_veiling_light +=  this->scene->camera_response.back() * temp_cur;

  wideband_veiling_light *= 1.0 / this->scene->K * this->scene->WAVELENGTHS_SUB;

  return wideband_veiling_light;
}


void NewModel::calc_attenuation(cv::Scalar color_1_obs, cv::Scalar color_2_obs, cv::Scalar wideband_veiling_light)
{
  // Calculate backscatter attenuation for each channel
  float channel_bs;
  for (int i = 0; i < 3; i++)
  {
    channel_bs =  (this->COLOR_1_TRUTH[i] * color_2_obs[i]) - (this->COLOR_2_TRUTH[i] * color_1_obs[i]) +
      (this->COLOR_2_TRUTH[i] - this->COLOR_1_TRUTH[i]) * wideband_veiling_light[i];
    channel_bs = channel_bs / ((this->COLOR_2_TRUTH[i] - this->COLOR_1_TRUTH[i]) * wideband_veiling_light[i]);
    this->backscatter_att[i] = -1.0 * log(channel_bs) / this->scene->DISTANCE;
  }

  // Calculate direct signal attenuation for each channel
  float channel_ds;
  for (int i = 0; i < 3; i++)
  {
    channel_ds =  color_2_obs[i] - wideband_veiling_light[i] *
      (1.0 - exp(-1.0 * this->backscatter_att[i] * this->scene->DISTANCE));
    channel_ds = channel_ds / this->COLOR_2_TRUTH[i];
    this->direct_signal_att[i] = -1.0 * log(channel_ds) / this->scene->DISTANCE;
  }
}


/** Set attenuation values from pre calculated attenuation values.
 */
void NewModel::est_attenuation()
{
  float round_depth = fabs((this->depth + 0.5) * 2);
  round_depth = roundf(round_depth * 1) / 2;

  this->backscatter_att[0] = this->att_map[round_depth][0];
  this->backscatter_att[1] = this->att_map[round_depth][1];
  this->backscatter_att[2] = this->att_map[round_depth][2];

  this->direct_signal_att[0] = this->att_map[round_depth][3];
  this->direct_signal_att[1] = this->att_map[round_depth][4];
  this->direct_signal_att[2] = this->att_map[round_depth][5];
}


void NewModel::initialize_file()
{
  TiXmlDeclaration * decl = new TiXmlDeclaration("1.0", "", "");
  this->out_doc.LinkEndChild(decl);

  this->file_initialized = true;
}


void NewModel::set_data_to_file()
{
  TiXmlElement * data_depth = new TiXmlElement("Depth");
  this->out_doc.LinkEndChild(data_depth);

  if (this->OPTIMIZE)
  {
    data_depth->SetDoubleAttribute("val", static_cast<double>(this->depth_max_range));
  }
  else
  {
    data_depth->SetDoubleAttribute("val", static_cast<double>(this->depth));
  }

  TiXmlElement * data_backscatter_att = new TiXmlElement("Backscatter_Attenuation");
  data_depth->LinkEndChild(data_backscatter_att);
  data_backscatter_att->SetDoubleAttribute("blue", this->backscatter_att[0]);
  data_backscatter_att->SetDoubleAttribute("green", this->backscatter_att[1]);
  data_backscatter_att->SetDoubleAttribute("red", this->backscatter_att[2]);

  TiXmlElement * data_direct_signal_att = new TiXmlElement("Direct_Signal_Attenuation");
  data_depth->LinkEndChild(data_direct_signal_att);
  data_direct_signal_att->SetDoubleAttribute("blue", this->direct_signal_att[0]);
  data_direct_signal_att->SetDoubleAttribute("green", this->direct_signal_att[1]);
  data_direct_signal_att->SetDoubleAttribute("red", this->direct_signal_att[2]);
}


void NewModel::end_file(std::string OUTPUT_FILENAME)
{
  this->out_doc.SaveFile(OUTPUT_FILENAME.c_str());
}


void NewModel::load_data(std::string INPUT_FILENAME)
{
  TiXmlDocument* in_doc = new TiXmlDocument(INPUT_FILENAME.c_str());
  if (!in_doc->LoadFile())
  {
    if (this->LOG_SCREEN)
    {
      std::cout << "ERROR: Could not load attenuation input file." << std::endl;
    }
    exit(EXIT_FAILURE);
  }
  else if (this->LOG_SCREEN)
  {
    std::cout << "LOG: Loaded attenuation input file." << std::endl;
  }

  double m_depth;
  double bs_blue, bs_green, bs_red;
  double ds_blue, ds_green, ds_red;

  TiXmlElement* pDepthNode = NULL;
  TiXmlElement* pAttNode = NULL;

  pDepthNode = in_doc->FirstChildElement("Depth");

  for (pDepthNode; pDepthNode; pDepthNode = pDepthNode->NextSiblingElement())
  {
    pDepthNode->QueryDoubleAttribute("val", &m_depth);

    pAttNode = pDepthNode->FirstChildElement("Backscatter_Attenuation");
    if (pAttNode)
    {
      pAttNode->QueryDoubleAttribute("blue", &bs_blue);
      pAttNode->QueryDoubleAttribute("green", &bs_green);
      pAttNode->QueryDoubleAttribute("red", &bs_red);
    }

    pAttNode = pAttNode->NextSiblingElement("Direct_Signal_Attenuation");
    if (pAttNode)
    {
      pAttNode->QueryDoubleAttribute("blue", &ds_blue);
      pAttNode->QueryDoubleAttribute("green", &ds_green);
      pAttNode->QueryDoubleAttribute("red", &ds_red);
    }

    this->att_map[m_depth] = {bs_blue, bs_green, bs_red, ds_blue, ds_green, ds_red};
  }

  if (this->LOG_SCREEN)
  {
    std::cout << "LOG: Added prior attenuation values to program." << std::endl;
  }
}

}  // namespace underwater_color_enhance
