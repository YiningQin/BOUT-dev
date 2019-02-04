#include "gtest/gtest.h"

#include "bout_types.hxx"
#include "fft.hxx"
#include "field3d.hxx"
#include "test_extras.hxx"
#include "bout/constants.hxx"
#include "bout/deriv_store.hxx"
#include "bout/paralleltransform.hxx"

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

// Some basic sanity checks for the derivative kernels. Checks the
// derivatives of sin(R) where R = {X, Y, Z} for each R
// individually. To make this as fast as possible, we use only a
// couple of points in the non-tested directions -- not just one
// though, as this allows us to check we're not introducing spurious
// variation in the other directions.
//
// This is one of the more complicated uses of googletest! We need to
// test all combinations of methods, directions and (standard)
// derivative types. Unfortunately, Z has one more method than X and Y
// (FFT), and the different orders have different sets of
// methods. This means we can't just use the provided `Combine` to
// produce the Cartesian product of methods, directions, and types as
// the first depends on the second two. Instead, we instantiate the
// tests separately for each direction and order and test all the
// methods for that direction in that instantiation.

namespace {
// These are merely sanity checks, so don't expect them to agree very
// well! This has to be sufficiently loose for the least accurate
// method to pass, or we need to also match up tolerances to methods
constexpr BoutReal derivatives_tolerance{5.e-3};
}

class DerivativesTest
    : public ::testing::TestWithParam<std::tuple<DIRECTION, DERIV, std::string>> {
public:
  DerivativesTest() : input{mesh}, expected{mesh} {

    // Make sure fft functions are both quiet and deterministic by
    // setting fft_measure to false
    bout::fft::fft_init(false);

    using Index = Field3D::ind_type;

    // Pointer to index method that converts single-index to 3-index space
    using DirectionFunction = int (Index::*)() const;
    DirectionFunction dir;

    // Number of guard cells in (x, y). The "other" direction will
    // have none
    int x_guards{0};
    int y_guards{0};

    // Grid sizes
    int nx{3};
    int ny{3};
    int nz{2};

    // This must be a balance between getting any kind of accuracy and
    // each derivative running in ~1ms or less
    constexpr int grid_size{64};
    const BoutReal box_length{TWOPI / grid_size};

    // Set all the variables for this direction
    // In C++14 this can be the more explicit std::get<DIRECTION>()
    switch (std::get<0>(GetParam())) {
    case DIRECTION::X:
      nx = grid_size;
      dir = &Index::x;
      x_guards = 2;
      region = RGN_NOX;
      break;
    case DIRECTION::Y:
      ny = grid_size;
      dir = &Index::y;
      y_guards = 2;
      region = RGN_NOY;
      break;
    case DIRECTION::Z:
      nz = grid_size;
      dir = &Index::z;
      region = RGN_ALL;
      break;
    default:
      throw BoutException("bad direction");
    }

    if (mesh != nullptr) {
      delete mesh;
      mesh = nullptr;
    }

    mesh = new FakeMesh(nx, ny, nz);

    mesh->xstart = x_guards;
    mesh->xend = nx - (x_guards + 1);
    mesh->ystart = y_guards;
    mesh->yend = ny - (y_guards + 1);

    output_info.disable();
    mesh->createDefaultRegions();
    output_info.enable();

    // Make the input and expected output fields
    // Weird `(i.*dir)()` syntax here in order to call the direction method
    // C++17 makes this nicer with std::invoke
    input = makeField<Field3D>([&](Index& i) { return std::sin((i.*dir)() * box_length); }, mesh);

    // Get the expected result for this order of derivative
    // Again, could be nicer in C++17 with std::get<DERIV>(GetParam())
    switch (std::get<1>(GetParam())) {
    case DERIV::Standard:
      expected = makeField<Field3D>(
        [&](Index& i) { return std::cos((i.*dir)() * box_length) * box_length; }, mesh);
      break;
    case DERIV::StandardSecond:
      expected = makeField<Field3D>(
        [&](Index& i) { return -std::sin((i.*dir)() * box_length) * pow(box_length, 2); },
        mesh);
      break;
    case DERIV::StandardFourth:
      expected = makeField<Field3D>(
        [&](Index& i) { return std::sin((i.*dir)() * box_length) * pow(box_length, 4); },
        mesh);
      break;
    default:
      throw BoutException("Sorry, don't we test that type of derivative yet!");
    }

    // We need the parallel slices for the y-direction
    ParallelTransformIdentity identity{};
    identity.calcYUpDown(input);

    // FIXME: remove when defaults are set in the DerivativeStore ctor
    DerivativeStore<Field3D>::getInstance().initialise(Options::getRoot());
  };

  Field3D input;
  Field3D expected;

  // Region not including the guard cells in current direction
  REGION region;
};

// Get all the available methods for this direction and turn it from a
// collection of strings to a collection of tuples of the direction,
// order, and strings so that the test fixture knows which direction
// we're using
auto getMethodsForDirection(DERIV derivative_order, DIRECTION direction)
    -> std::vector<std::tuple<DIRECTION, DERIV, std::string>> {

  auto available_methods = DerivativeStore<Field3D>::getInstance().getAvailableMethods(
      derivative_order, direction);

  // Method names together with the current direction and derivative type
  std::vector<std::tuple<DIRECTION, DERIV, std::string>> methods{};

  std::transform(std::begin(available_methods), std::end(available_methods),
                 std::back_inserter(methods), [&](std::string method) {
                   return std::make_tuple(direction, derivative_order, method);
                 });

  return methods;
};

// Returns the method out of the direction/order/method tuple for
// printing the test name
auto methodDirectionTupleToString(
    const ::testing::TestParamInfo<std::tuple<DIRECTION, DERIV, std::string>>& param)
    -> std::string {
  return std::get<2>(param.param);
}

// Instantiate the test for X, Y, Z for first derivatives
INSTANTIATE_TEST_CASE_P(FirstX, DerivativesTest,
                        ::testing::ValuesIn(getMethodsForDirection(DERIV::Standard,
                                                                   DIRECTION::X)),
                        methodDirectionTupleToString);

INSTANTIATE_TEST_CASE_P(FirstY, DerivativesTest,
                        ::testing::ValuesIn(getMethodsForDirection(DERIV::Standard,
                                                                   DIRECTION::Y)),
                        methodDirectionTupleToString);

INSTANTIATE_TEST_CASE_P(FirstZ, DerivativesTest,
                        ::testing::ValuesIn(getMethodsForDirection(DERIV::Standard,
                                                                   DIRECTION::Z)),
                        methodDirectionTupleToString);

// Instantiate the test for X, Y, Z for second derivatives
INSTANTIATE_TEST_CASE_P(SecondX, DerivativesTest,
                        ::testing::ValuesIn(getMethodsForDirection(DERIV::StandardSecond,
                                                                   DIRECTION::X)),
                        methodDirectionTupleToString);

INSTANTIATE_TEST_CASE_P(SecondY, DerivativesTest,
                        ::testing::ValuesIn(getMethodsForDirection(DERIV::StandardSecond,
                                                                   DIRECTION::Y)),
                        methodDirectionTupleToString);

INSTANTIATE_TEST_CASE_P(SecondZ, DerivativesTest,
                        ::testing::ValuesIn(getMethodsForDirection(DERIV::StandardSecond,
                                                                   DIRECTION::Z)),
                        methodDirectionTupleToString);

// Instantiate the test for X, Y, Z for fourth derivatives
INSTANTIATE_TEST_CASE_P(FourthX, DerivativesTest,
                        ::testing::ValuesIn(getMethodsForDirection(DERIV::StandardFourth,
                                                                   DIRECTION::X)),
                        methodDirectionTupleToString);

INSTANTIATE_TEST_CASE_P(FourthY, DerivativesTest,
                        ::testing::ValuesIn(getMethodsForDirection(DERIV::StandardFourth,
                                                                   DIRECTION::Y)),
                        methodDirectionTupleToString);

INSTANTIATE_TEST_CASE_P(FourthZ, DerivativesTest,
                        ::testing::ValuesIn(getMethodsForDirection(DERIV::StandardFourth,
                                                                   DIRECTION::Z)),
                        methodDirectionTupleToString);

// All standard derivatives have the same signature, so we can use a
// single test, just instantiate it for each direction/order combination
TEST_P(DerivativesTest, Sanity) {
  auto derivative = DerivativeStore<Field3D>::getInstance().getStandardDerivative(
    std::get<2>(GetParam()), std::get<0>(GetParam()), STAGGER::None, std::get<1>(GetParam()));

  Field3D result{mesh};
  result.allocate();
  derivative(input, result, region);

  EXPECT_TRUE(IsField3DEqualField3D(result, expected, "RGN_NOBNDRY",
                                    derivatives_tolerance));
}
