#ifndef CONFIDENCE_INTERVAL_H
#define CONFIDENCE_INTERVAL_H

#include <memory>
#include <algorithm>
#include <cmath>

enum Alpha {
		percent90,
		percent95,
		percent99
};

enum IntervalType {
		CD,
		StudentCoefficient
};

enum CalcValue {
		Mean,
		Median,
		Mode
};

class ConfidenceInterval {
public:
	
	ConfidenceInterval(size_t size = 2, Alpha alpha = percent90, IntervalType intervalType = CD, CalcValue calcValue = Mean) {
		SetConfidenceIntervalOptions(size, alpha, intervalType, calcValue);
	}

	void SetConfidenceIntervalOptions(size_t size, Alpha alpha = percent90, IntervalType intervalType = CD, CalcValue calcValue = Mean) {
		_size = size;
		_array = std::make_unique<double[]>(_size);

		_alpha = alpha;
		_intervalType = intervalType;
		_calcValue = calcValue;
		isEmpty = false;
	}

	double calculateInterval() {
		if (!isEmpty) {
			double x = getIntervalCalcValue();
			if (x == -1) return -1;
		
			double value;
	
			value = getIntervalValue(x);
			
			return value == -1 ? -1 : value;
		}
		return -1;	
	}

	void setValue(size_t index, double value) {
		if (!isEmpty) {
			_array[index] = value;
		}
	}

	size_t getSize() const {
		if (!isEmpty)
			return _size;
		return 0;
	}

	bool empty() {
		if (isEmpty) return true;
    	return false;
	}
private:
	Alpha _alpha;
	IntervalType _intervalType;
	CalcValue _calcValue;
	std::unique_ptr<double[]> _array;
	size_t _size;
	
	bool isEmpty;

	double statCom(double q, int i, int j) {
		double zz = 1, z = zz;
		int k = i;
		while (k <= j) { zz *= q * k / (k + 1); z += zz; k += 2; }
		return z;
	}
	
	double tStatistic(double t, int n) {
		double th = atan(std::abs(t) / sqrt((double)n)), pi2 = acos((double)-1) / 2;

		if (n == 1) return (1 - th / pi2);

		double sth = sin(th), cth = cos(th);

		if (n % 2 == 1) return (1 - (th + sth * cth * statCom(cth * cth, 2, n - 3)) / pi2);
		return (1 - sth * statCom(cth * cth, 1, n - 3));
	}

	double studentCoefficient(double alpha, int n) {
		double v = 0.5, dv = 0.5, t = 0;
		while (dv > 1e-10)
		{
			t = 1 / v - 1; dv /= 2;
			if (tStatistic(t, n) > alpha) v -= dv;
			else v += dv;
		}
		return t;
	}

	double getMeanValue() {
		double mean = 0;
		for (int i = 0; i < _size; i++)
		{
			mean += _array[i];
		}
		return mean /= _size;
	}

	double getMedianValue() {
		double median;
		double* newArray = new double[_size];
		for (int i = 0; i < _size; i++) {
			newArray[i] = _array[i];
		}
		std::sort(newArray, newArray + _size);
		median = newArray[_size / 2];
		delete[] newArray;
		return median;
	}

	double getModeValue() {
		double mode = _array[0];
		int currentMax = 0, generalMax = 0;
		for (int i = 0; i < _size; i++) {
			if (currentMax > generalMax) {
				generalMax = currentMax;
				mode = _array[i - 1];
			}
			currentMax = 0;
			for (int j = i; j < _size; j++)
				if (_array[j] == _array[i])
					currentMax++;
		}
		return mode;
	}

	double getSDValue(double mean) {
		double sd = 0;
		for (int i = 0; i < _size; i++) {
			sd += (_array[i] - mean) * (_array[i] - mean);
		}
		sd /= (_size - 1.0);
		return std::sqrt(sd);
	}

	double getConfidenceIntervalCD(double x) {
		double mean = getMeanValue();
		double sd = getSDValue(mean);
		int newsize = 0;
		mean = 0;
		for (int i = 0; i < _size; i++)
		{
			if (x - sd <= _array[i] && _array[i] <= x + sd)
			{
				mean += _array[i];
				newsize++;
			}
		}
		return newsize == 0 ? mean : mean / newsize;
	}

	double getConfidenceIntervalStudentCoefficient(double x, double t) {
		double mean = getMeanValue();
		double sd = getSDValue(mean);
		int newsize = 0;
		sd /= std::sqrt(_size);
		sd *= t;
		mean = 0;
		for (int i = 0; i < _size; i++)
		{
			if (x - sd <= _array[i] && _array[i] <= x + sd)
			{
				mean += _array[i];
				newsize++;
			}
		}
		return newsize == 0 ? mean : mean / newsize;
	}

	double getIntervalCalcValue() {
		switch (_calcValue) {
			case CalcValue::Mean: {
				return getMeanValue();
			}
			case CalcValue::Median: {
				return getMedianValue();
			}
			case CalcValue::Mode: {
				return getModeValue();
			}
			default: {
				return -1;
			}
		}
	}

	double getIntervalStudentCoefficientValue(double x) {
		switch (_alpha) {
			case Alpha::percent90: {
				return getConfidenceIntervalStudentCoefficient(x, 10 * 0.01);
			}
			case Alpha::percent95: {
				return getConfidenceIntervalStudentCoefficient(x, 5 * 0.01);
			}
			case Alpha::percent99: {
				return getConfidenceIntervalStudentCoefficient(x, 1 * 0.01);
			}
			default: {
				return -1;
			}
		}
	}

	double getIntervalValue(double x) {
		switch (_intervalType) {
			case IntervalType::CD: {
				return getConfidenceIntervalCD(x);
			}
			case IntervalType::StudentCoefficient: {
				return getIntervalStudentCoefficientValue(x);
			}
			default: {
				return -1;
			}
		}
	}
};


#endif