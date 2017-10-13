#include "PixelClassificationHE.h"

#include <boost\filesystem.hpp>


#include "HSD/BackgroundMask.h"
#include "IO/Logging/LogHandler.h"
#include "LevelReading.h"
#include "MiscFunctionality.h"

PixelClassificationHE::PixelClassificationHE(bool consider_ink, size_t log_file_id, std::string& debug_dir) : m_consider_ink_(consider_ink), m_log_file_id_(log_file_id), m_debug_dir_(debug_dir)
{
}

SampleInformation PixelClassificationHE::GenerateCxCyDSamples(
	MultiResolutionImage& tile_reader,
	cv::Mat& static_image,
	std::vector<cv::Point>& tile_coordinates,
	uint32_t tile_size,
	size_t training_size,
	size_t min_training_size,
	uint32_t min_level,
	float hema_percentile,
	float eosin_percentile,
	bool is_tiff,
	std::vector<double> &spacing)
{
	IO::Logging::LogHandler* logger_instance(IO::Logging::LogHandler::GetInstance());

	logger_instance->QueueCommandLineLogging("Minimum number of samples to take from the WSI: " + std::to_string(training_size), IO::Logging::NORMAL);
	logger_instance->QueueCommandLineLogging("Minimum number of samples to take from each patch: " + std::to_string(min_training_size), IO::Logging::NORMAL);

	bool multiresolution_image = false;
	if (!static_image.data)
	{
		multiresolution_image = true;
	}
	//int MinTrainSize = 0;// was 100000
	else
	{
		min_training_size = 0;
	}

	size_t selected_images_count = 0;

	// Tracks the created samples for each class.
	size_t total_hema_count = 0;
	size_t total_eosin_count = 0;
	size_t total_background_count = 0;

	SampleInformation sample_information{ cv::Mat::zeros(training_size, 1, CV_32FC1),
		cv::Mat::zeros(training_size, 1, CV_32FC1),
		cv::Mat::zeros(training_size, 1, CV_32FC1),
		cv::Mat::zeros(training_size, 1, CV_32FC1) };

	std::vector<uint32_t> random_numbers(ASAP::MiscFunctionality::CreateListOfRandomIntegers(tile_coordinates.size()));
	for (size_t current_tile = 0; current_tile < tile_coordinates.size(); ++current_tile)
	{
		logger_instance->QueueCommandLineLogging(std::to_string(current_tile + 1) + " images taken as examples!", IO::Logging::NORMAL);
		logger_instance->QueueFileLogging("=============================\nRandom image number: " + std::to_string(current_tile + 1) + "=============================", m_log_file_id_, IO::Logging::NORMAL);

		//===========================================================================
		//	HSD / CxCy Color Model
		//===========================================================================

		unsigned char* data(new unsigned char[tile_size * tile_size * 4]);

		tile_reader.getRawRegion(
		tile_coordinates[random_numbers[current_tile]].x * tile_reader.getLevelDownsample(0),
		tile_coordinates[random_numbers[current_tile]].y * tile_reader.getLevelDownsample(0),
		tile_size, tile_size, min_level, data);

		cv::Mat raw_image = cv::Mat::zeros(tile_size, tile_size, CV_8UC3);
		LevelReading::ArrayToMatrix(data, raw_image, false, is_tiff);

		if (IO::Logging::LogHandler::GetInstance()->GetOutputLevel() == IO::Logging::DEBUG && !m_debug_dir_.empty())
		{
			boost::filesystem::path debug_directory_path(m_debug_dir_ + "/DebugData/ClassificationResult");
			boost::filesystem::create_directory(debug_directory_path);

			std::string original_name = debug_directory_path.string() + "/ImageRaw" + std::to_string(selected_images_count) + ".tif";
			cv::imwrite(original_name, raw_image);
		}

		HSD::HSD_Model hsd_image = multiresolution_image ? HSD::HSD_Model(raw_image, HSD::CHANNEL_SHIFT) : HSD::HSD_Model(static_image, HSD::CHANNEL_SHIFT);

		//===========================================================================
		//	Background Mask
		//===========================================================================
		cv::Mat background_mask(HSD::BackgroundMask::CreateBackgroundMask(hsd_image, 0.24, 0.22));

		//*************************************************************************
		// Sample extraction with Hough Transform
		//*************************************************************************
			// Attempts to acquire the HE stain masks, followed by the classification of the image. Which results in tissue, class, train and test data.
		std::pair<HematoxylinMaskInformation, EosinMaskInformation> he_masks(Create_HE_Masks_(hsd_image, background_mask, min_training_size, hema_percentile, eosin_percentile, spacing, multiresolution_image));
		HE_Staining::HE_Classifier he_classifier;
		HE_Staining::ClassificationResults classification_results(he_classifier.Classify(hsd_image, background_mask, he_masks.first, he_masks.second));

		// Wanna keep?
		// Randomly pick samples and Fill in Cx-Cy-D major sample vectors	
		if (classification_results.train_and_class_data.train_data.rows >= min_training_size)
		{
			if (logger_instance->GetOutputLevel() == IO::Logging::DEBUG && !m_debug_dir_.empty())
			{
				cv::imwrite(m_debug_dir_ + "/debug_data/classification_result/classified" + std::to_string(selected_images_count) + ".tif", classification_results.all_classes * 100);
			}

			sample_information = InsertTrainingData_(hsd_image, classification_results, he_masks.first, he_masks.second, total_hema_count, total_eosin_count, total_background_count, training_size);
			++selected_images_count;

			size_t hema_count_real = total_hema_count > training_size * 9 / 20 ? hema_count_real = training_size * 9 / 20 : total_hema_count;
			size_t eosin_count_real = total_eosin_count > training_size * 9 / 20 ? eosin_count_real = training_size * 9 / 20 : total_eosin_count;
			size_t background_count_real = total_background_count > training_size * 1 / 10 ? background_count_real = training_size * 1 / 10 : total_background_count;

			logger_instance->QueueCommandLineLogging(
				std::to_string(hema_count_real + eosin_count_real + background_count_real) +
				" training sets are filled, out of " + std::to_string(training_size) + " required.",
				IO::Logging::NORMAL);

			logger_instance->QueueFileLogging("Filled: " + std::to_string(hema_count_real + eosin_count_real + background_count_real) + " / " + std::to_string(training_size),
				m_log_file_id_,
				IO::Logging::NORMAL);

			logger_instance->QueueFileLogging("Hema: " + std::to_string(hema_count_real) + ", Eos: " + std::to_string(eosin_count_real) + ", BG: " + std::to_string(background_count_real),
				m_log_file_id_,
				IO::Logging::NORMAL);
		}

		if (total_hema_count >= training_size * 9 / 20 && total_eosin_count >= training_size * 9 / 20 && total_background_count >= training_size / 10)
		{
			break;
		}
	}

	if ((total_hema_count < training_size * 9 / 20 || total_eosin_count < training_size * 9 / 20 || total_background_count < training_size / 10))
	{
		size_t non_zero_class_pixels = cv::countNonZero(sample_information.class_data);

		if (non_zero_class_pixels != sample_information.class_data.rows && (selected_images_count > 2 || !min_training_size))
		{
			std::string log_text("Could not fill all the " + std::to_string(training_size) + " samples required. Continuing with what is left...");
			logger_instance->QueueCommandLineLogging(log_text, IO::Logging::NORMAL);
			logger_instance->QueueFileLogging(log_text, m_log_file_id_, IO::Logging::NORMAL);

			sample_information = std::move(PatchTestData_(non_zero_class_pixels, sample_information));
		}
	}

	return sample_information;
}

std::pair<HematoxylinMaskInformation, EosinMaskInformation> PixelClassificationHE::Create_HE_Masks_(
	HSD::HSD_Model& hsd_image,
	cv::Mat& background_mask,
	size_t min_training_size,
	float hema_percentile,
	float eosin_percentile,
	std::vector<double>& spacing,
	bool is_multiresolution)
{
	// Sets the variables for the ellipse detection. 
	HoughTransform::RandomizedHoughTransformParameters parameters;
	parameters.min_ellipse_radius	= floor(1.94 / spacing[0]);
	parameters.max_ellipse_radius	= ceil(4.86 / spacing[0]);
	parameters.epoch_size				= 2;
	parameters.count_threshold			= 5;

	int sigma = 4;
	int low_threshold = 45;
	int high_threshold = 80;

	// Attempts to detect ellispes. Applies a blur and canny edge operation on the density matrix before detecting ellipses through a randomized Hough transform.
	std::vector<HoughTransform::Ellipse> detected_ellipses(HE_Staining::MaskGeneration::DetectEllipses(hsd_image.density, sigma, low_threshold, high_threshold, parameters));

	// Prepares the logger and a string holding potential failure messages.
	IO::Logging::LogHandler* logger_instance(IO::Logging::LogHandler::GetInstance());
	std::string failure_log_message;

	std::pair<HE_Staining::HematoxylinMaskInformation, HE_Staining::EosinMaskInformation> mask_acquisition_results;

	double min_detected_ellipses = hsd_image.red_density.rows * hsd_image.red_density.rows * spacing[0] * spacing[0] * (150 / 247700.0);
	if (detected_ellipses.size() > min_detected_ellipses || (detected_ellipses.size() > 10 && !is_multiresolution))
	{
		logger_instance->QueueFileLogging("Passed step 1: Number of nuclei " + std::to_string(detected_ellipses.size()), m_log_file_id_, IO::Logging::NORMAL);

		std::pair<bool, HE_Staining::HematoxylinMaskInformation> mask_acquisition_result;
		if (!m_consider_ink_ || (mask_acquisition_result = HE_Staining::MaskGeneration::GenerateHematoxylinMasks(hsd_image, background_mask, detected_ellipses, hema_percentile)).first)
		{
			logger_instance->QueueFileLogging("Skipped - May contain INK.", m_log_file_id_, IO::Logging::NORMAL);
		}

		// Creates a reference of the hema mask information, for ease of use. And copies the results into the result pairing.
		HE_Staining::HematoxylinMaskInformation& hema_mask_info(mask_acquisition_result.second);
		mask_acquisition_results.first = hema_mask_info;

		// Test and Train data Generator
		if (hema_mask_info.training_pixels > min_training_size / 2)
		{
			logger_instance->QueueFileLogging(
				"Passed step 2: Amount of Hema samples " + std::to_string(hema_mask_info.training_pixels) + ", more than the limit of " + std::to_string(min_training_size / 2),
				m_log_file_id_,
				IO::Logging::NORMAL);

			HE_Staining::EosinMaskInformation eosin_mask_info(HE_Staining::MaskGeneration::GenerateEosinMasks(hsd_image, background_mask, hema_mask_info, eosin_percentile));
			if (eosin_mask_info.training_pixels > min_training_size / 2)
			{
				mask_acquisition_results.second = eosin_mask_info;

				IO::Logging::LogHandler::GetInstance()->QueueFileLogging(
					"Passed step 3: Amount of Eos samples " + std::to_string(eosin_mask_info.training_pixels) + ", more than the limit of " + std::to_string(min_training_size / 2),
					m_log_file_id_,
					IO::Logging::NORMAL);
			}
			else
			{
				failure_log_message = "Skipped step 3: Amount of Eos samples " + std::to_string(eosin_mask_info.training_pixels) + ", less than the limit of " + std::to_string(min_training_size / 2);
			}
		}
		else
		{
			failure_log_message = "Skipped step 2: Amount of Hema samples " + std::to_string(hema_mask_info.training_pixels) + ", less than the limit of " + std::to_string(min_training_size / 2);
		}
	}
	else
	{
		failure_log_message = "Skipped: Nuclei " + std::to_string(detected_ellipses.size()) + " < " + std::to_string(min_detected_ellipses);
	}

	if (!failure_log_message.empty())
	{
		logger_instance->QueueFileLogging(failure_log_message, m_log_file_id_, IO::Logging::NORMAL);
	}

	return mask_acquisition_results;
}

SampleInformation PixelClassificationHE::InsertTrainingData_(
	HSD::HSD_Model& hsd_image,
	ClassificationResults& classification_results,
	HematoxylinMaskInformation& hema_mask_info,
	EosinMaskInformation& eosin_mask_info,
	size_t& total_hema_count,
	size_t& total_eosin_count,
	size_t& total_background_count,
	size_t training_size)
{
	// Creates a list of random values, ranging from 0 to the amount of class pixels - 1.
	std::vector<uint32_t> hema_random_numbers(ASAP::MiscFunctionality::CreateListOfRandomIntegers(classification_results.hema_pixels));
	std::vector<uint32_t> eosin_random_numbers(ASAP::MiscFunctionality::CreateListOfRandomIntegers(classification_results.eosin_pixels));
	std::vector<uint32_t> background_random_numbers(ASAP::MiscFunctionality::CreateListOfRandomIntegers(classification_results.background_pixels));

	// Tracks the amount of class specific sample counts. And the matrices which will hold information for each pixel classified as their own.
	size_t local_hema_count = 0;
	size_t local_eosin_count = 0;
	size_t local_background_count = 0;
	cv::Mat train_data_hema(cv::Mat::zeros(classification_results.hema_pixels, 4, CV_32FC1));
	cv::Mat train_data_eosin(cv::Mat::zeros(classification_results.eosin_pixels, 4, CV_32FC1));
	cv::Mat train_data_background(cv::Mat::zeros(classification_results.background_pixels, 4, CV_32FC1));

	// Creates a matrix per class, holding the c_x, c_y and density channels per classified pixel.
	cv::Mat& all_classes(classification_results.all_classes);
	for (int row = 0; row < all_classes.rows; ++row)
	{
		uchar* Class = all_classes.ptr(row);
		for (int col = 0; col < all_classes.cols; ++col)
		{
			if (*Class == 1)
			{
				train_data_hema.at<float>(local_hema_count, 0) = hsd_image.c_x.at<float>(row, col);
				train_data_hema.at<float>(local_hema_count, 1) = hsd_image.c_y.at<float>(row, col);
				train_data_hema.at<float>(local_hema_count, 2) = hsd_image.density.at<float>(row, col);
				train_data_hema.at<float>(local_hema_count, 3) = 1;
				++local_hema_count;
			}
			else if (*Class == 2)
			{
				train_data_eosin.at<float>(local_eosin_count, 0) = hsd_image.c_x.at<float>(row, col);
				train_data_eosin.at<float>(local_eosin_count, 1) = hsd_image.c_y.at<float>(row, col);
				train_data_eosin.at<float>(local_eosin_count, 2) = hsd_image.density.at<float>(row, col);
				train_data_eosin.at<float>(local_eosin_count, 3) = 2;
				++local_eosin_count;
			}
			else if (*Class == 3)
			{
				train_data_background.at<float>(local_background_count, 0) = hsd_image.c_x.at<float>(row, col);
				train_data_background.at<float>(local_background_count, 1) = hsd_image.c_y.at<float>(row, col);
				train_data_background.at<float>(local_background_count, 2) = hsd_image.density.at<float>(row, col);
				train_data_background.at<float>(local_background_count, 3) = 3;
				++local_background_count;
			}
			*Class++;
		}
	}

	SampleInformation sample_information;

	for (size_t hema_pixel = 0; hema_pixel < classification_results.hema_pixels / 2; ++hema_pixel)
	{
		if (hema_pixel + total_hema_count < training_size * 9 / 20)
		{
			sample_information.training_data_c_x.at<float>(hema_pixel + total_hema_count, 0) = train_data_hema.at<float>(hema_random_numbers[hema_pixel], 0);
			sample_information.training_data_c_y.at<float>(hema_pixel + total_hema_count, 0) = train_data_hema.at<float>(hema_random_numbers[hema_pixel], 1);
			sample_information.training_data_density.at<float>(hema_pixel + total_hema_count, 0) = train_data_hema.at<float>(hema_random_numbers[hema_pixel], 2);
			sample_information.class_data.at<float>(hema_pixel + total_hema_count, 0) = 1;
		}
	}
	for (size_t eosin_pixel = 0; eosin_pixel < classification_results.eosin_pixels / 2; ++eosin_pixel)
	{
		if (eosin_pixel + total_eosin_count + training_size * 9 / 20 < training_size * 18 / 20)
		{
			sample_information.training_data_c_x.at<float>(eosin_pixel + total_eosin_count + training_size * 9 / 20, 0) = train_data_eosin.at<float>(eosin_random_numbers[eosin_pixel], 0);
			sample_information.training_data_c_y.at<float>(eosin_pixel + total_eosin_count + training_size * 9 / 20, 0) = train_data_eosin.at<float>(eosin_random_numbers[eosin_pixel], 1);
			sample_information.training_data_density.at<float>(eosin_pixel + total_eosin_count + training_size * 9 / 20, 0) = train_data_eosin.at<float>(eosin_random_numbers[eosin_pixel], 2);
			sample_information.class_data.at<float>(eosin_pixel + total_eosin_count + training_size * 9 / 20, 0) = 2;
		}
	}
	for (size_t background_pixel = 0; background_pixel < classification_results.background_pixels / 2; ++background_pixel)
	{
		if (background_pixel + total_background_count + training_size * 18 / 20 < training_size)
		{
			sample_information.training_data_c_x.at<float>(background_pixel + total_background_count + training_size * 18 / 20, 0) = train_data_background.at<float>(background_random_numbers[background_pixel], 0);
			sample_information.training_data_c_y.at<float>(background_pixel + total_background_count + training_size * 18 / 20, 0) = train_data_background.at<float>(background_random_numbers[background_pixel], 1);
			sample_information.training_data_density.at<float>(background_pixel + total_background_count + training_size * 18 / 20, 0) = train_data_background.at<float>(background_random_numbers[background_pixel], 2);
			sample_information.class_data.at<float>(background_pixel + total_background_count + training_size * 18 / 20, 0) = 3;
		}
	}

	// Adds the local counts to the total count.
	total_hema_count += local_hema_count / 2;
	total_eosin_count += local_eosin_count / 2;
	total_background_count += local_background_count / 2;

	return sample_information;
}

SampleInformation PixelClassificationHE::PatchTestData_(size_t non_zero_count, SampleInformation& current_sample_information)
{
	SampleInformation new_sample_information{ cv::Mat::zeros(non_zero_count, 1, CV_32FC1),
												cv::Mat::zeros(non_zero_count, 1, CV_32FC1),
												cv::Mat::zeros(non_zero_count, 1, CV_32FC1),
												cv::Mat::zeros(non_zero_count, 1, CV_32FC1) };

	size_t added_samples_count = 0;
	for (size_t count = 0; count < current_sample_information.class_data.rows; ++count)
	{
		if (current_sample_information.class_data.at<float>(count, 0) != 0)
		{
			new_sample_information.training_data_c_x.at<float>(added_samples_count, 0) = current_sample_information.training_data_c_x.at<float>(count, 0);
			new_sample_information.training_data_c_y.at<float>(added_samples_count, 0) = current_sample_information.training_data_c_y.at<float>(count, 0);
			new_sample_information.training_data_density.at<float>(added_samples_count, 0) = current_sample_information.training_data_density.at<float>(count, 0);
			new_sample_information.class_data.at<float>(added_samples_count, 0) = current_sample_information.class_data.at<float>(count, 0);
			++added_samples_count;
		}
	}

	return new_sample_information;
}