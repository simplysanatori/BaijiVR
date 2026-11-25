// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vector>

#include <gtest/gtest.h>

#include "VideoCommon/VR/Perf/DynamicResolutionController.h"

using namespace VR::DynamicResolution;

namespace
{
constexpr int FLOOR = 2;

// A second of a 60 fps game at 120 Hz that keeps up.
Sample At(double gpu_ms, int scale)
{
  Sample s;
  s.running = true;
  s.period_ms = 1000.0 / 120.0;
  s.refreshes = 120;
  s.fresh = 60;
  s.gpu_frames = 60;
  s.gpu_ms = gpu_ms;
  s.scale = scale;
  return s;
}

Sample At30(double gpu_ms, int scale)
{
  Sample s = At(gpu_ms, scale);
  s.fresh = s.gpu_frames = 30;
  return s;
}

// A game whose GPU cost is given per scale.
struct Game
{
  Controller controller;
  int max_up = 2;
  int ceiling_up = 2;
  int scale = FLOOR;

  Controller::Step Second(double gpu_ms)
  {
    scale = FLOOR + controller.Judge(At(gpu_ms, scale), FLOOR, max_up, ceiling_up);
    return controller.LastStep();
  }
  template <typename Cost>
  void Run(int seconds, Cost&& cost)
  {
    for (int i = 0; i < seconds; ++i)
      Second(cost(scale));
  }
  void Run(int seconds, double gpu_ms)
  {
    Run(seconds, [gpu_ms](int) { return gpu_ms; });
  }
};

// Up one scale, settled.
Game AtOneUp()
{
  Game game;
  game.Run(Controller::UP_AFTER, 3.0);
  EXPECT_EQ(game.scale, 3);
  game.Run(Controller::SETTLE, 7.0);
  return game;
}
}  // namespace

TEST(DynamicResolution, AFullPeriodOfGpuTimeIsNotTheLimitAt60Fps)
{
  // 60 fps at 120 Hz: the frame has two refreshes.
  EXPECT_FALSE(Controller::Bad(At(9.0, 2)));
  EXPECT_FALSE(Controller::Bad(At(14.0, 2)));
  EXPECT_TRUE(Controller::Bad(At(14.5, 2)));
  // A 30 fps game has four.
  EXPECT_FALSE(Controller::Bad(At30(28.0, 2)));
  EXPECT_TRUE(Controller::Bad(At30(29.0, 2)));
}

TEST(DynamicResolution, PairsLeftOutCountAsFrames)
{
  // Every second pair is left out: 30 are shown, and the game's frame time is still 16.7 ms.
  Sample s = At30(20.0, 2);
  s.skipped = 30;
  EXPECT_NEAR(Controller::Interval(s), 1000.0 / 60.0, 1e-9);
  EXPECT_TRUE(Controller::Bad(s));
}

TEST(DynamicResolution, AFewPairsLeftOutNeedABusyGpu)
{
  Sample s = At(11.0, 2);
  s.fresh = s.gpu_frames = 57;
  s.skipped = 3;
  EXPECT_TRUE(Controller::Bad(s));
  s.gpu_ms = 5.0;
  EXPECT_FALSE(Controller::Bad(s));
  s.gpu_ms = 11.0;
  s.skipped = 1;
  EXPECT_FALSE(Controller::Bad(s));
}

TEST(DynamicResolution, ASlowGameWithAnIdleGpuIsTheEmulations)
{
  Sample s = At(4.0, 2);
  s.fresh = s.gpu_frames = 35;
  s.speed = 0.6;
  EXPECT_FALSE(Controller::Bad(s));
  // Nor is it room for more.
  EXPECT_FALSE(Controller{}.Good(s));
}

TEST(DynamicResolution, ASlowGameThatWaitsForTheGpuIsTheGpus)
{
  Sample s = At(16.0, 2);
  s.fresh = s.gpu_frames = 45;
  s.speed = 0.75;
  EXPECT_FALSE(Controller::Bad(s));  // 22 ms
  s.gpu_wait_ms = 45 * 5.0;
  EXPECT_TRUE(Controller::Bad(s));
  // Full speed, or an idle GPU: waits alone are nothing.
  s.speed = 1.0;
  EXPECT_FALSE(Controller::Bad(s));
  s.speed = 0.75;
  s.gpu_ms = 4.0;
  s.gpu_wait_ms = 45 * 20.0;
  EXPECT_FALSE(Controller::Bad(s));
}

TEST(DynamicResolution, LostRefreshesWithTheGpuBusyAreTheGpus)
{
  Sample s = At(9.0, 2);
  s.refreshes = 100;
  EXPECT_TRUE(Controller::Bad(s));
  s.gpu_ms = 6.0;
  EXPECT_FALSE(Controller::Bad(s));
  EXPECT_FALSE(Controller{}.Good(s));
  // Paced to the game (lazy heartbeat) there is a refresh per picture.
  s.gpu_ms = 9.0;
  s.refreshes = 60;
  s.every_refresh = false;
  EXPECT_FALSE(Controller::Bad(s));
}

TEST(DynamicResolution, TooFewPairsSayNothing)
{
  Sample s = At(300.0, 2);
  s.fresh = s.gpu_frames = 3;
  EXPECT_FALSE(Controller::Bad(s));
  EXPECT_FALSE(Controller{}.Good(s));
  // Immersive: nothing is timed.
  s = At(0.0, 2);
  s.gpu_frames = 0;
  EXPECT_FALSE(Controller::Bad(s));
  EXPECT_FALSE(Controller{}.Good(s));
}

TEST(DynamicResolution, StartsAtTheSettingAndGoesUpWithRoom)
{
  Game game;
  game.Run(Controller::UP_AFTER - 1, 3.0);
  EXPECT_EQ(game.scale, 2);
  EXPECT_EQ(game.Second(3.0), Controller::Step::Up);
  EXPECT_EQ(game.scale, 3);
  // Two steps at most.
  game.Run(200, 1.0);
  EXPECT_EQ(game.scale, 4);
}

TEST(DynamicResolution, NoStepUpOnAPredictedCostOverTheBudget)
{
  // 2x to 3x has 2.25 times the pixels; 0.75 * 0.85 * 16.7 ms is 10.6 ms.
  Game game;
  game.Run(60, 4.8);
  EXPECT_EQ(game.scale, 2);
  game.Run(Controller::UP_AFTER, 4.6);
  EXPECT_EQ(game.scale, 3);
}

TEST(DynamicResolution, A30FpsGameGoesUpOnTwoRefreshes)
{
  Controller controller;
  // The frame has 33 ms, the prediction may have 10.6.
  EXPECT_FALSE(controller.Good(At30(4.8, 2)));
  EXPECT_TRUE(controller.Good(At30(4.6, 2)));
  // Of a 60 fps game at 60 Hz it is the frame time.
  Sample s = At(4.8, 2);
  s.period_ms = 1000.0 / 60.0;
  s.refreshes = 60;
  EXPECT_FALSE(controller.Good(s));
}

TEST(DynamicResolution, WaitsCountForAStepUp)
{
  Sample s = At(3.0, 2);
  EXPECT_TRUE(Controller{}.Good(s));
  s.gpu_wait_ms = 60 * 3.0;
  EXPECT_FALSE(Controller{}.Good(s));
}

TEST(DynamicResolution, DownAfterFiveBadSecondsInARow)
{
  Game game = AtOneUp();
  game.Run(Controller::DOWN_AFTER - 1, 16.0);
  EXPECT_EQ(game.scale, 3);
  game.Second(12.0);
  game.Run(Controller::DOWN_AFTER - 1, 16.0);
  EXPECT_EQ(game.scale, 3);
  EXPECT_EQ(game.Second(16.0), Controller::Step::Down);
  EXPECT_EQ(game.scale, 2);
  // Never under the setting.
  game.Run(100, 40.0);
  EXPECT_EQ(game.scale, 2);
}

TEST(DynamicResolution, SettleIsNotJudged)
{
  Game game;
  game.Run(Controller::UP_AFTER, 3.0);
  ASSERT_EQ(game.scale, 3);
  game.Run(Controller::SETTLE + Controller::DOWN_AFTER - 1, 30.0);
  EXPECT_EQ(game.scale, 3);
  game.Second(30.0);
  EXPECT_EQ(game.scale, 2);
}

TEST(DynamicResolution, WaitsForTheVideoThreadToDrawAtTheNewScale)
{
  Game game;
  game.Run(Controller::UP_AFTER, 3.0);
  ASSERT_EQ(game.scale, 3);
  // Still at 2x (the game shows nothing new): not judged, however long.
  for (int i = 0; i < 30; ++i)
    EXPECT_EQ(game.controller.Judge(At(30.0, 2), FLOOR, 2), 1);
}

TEST(DynamicResolution, PausesAndFastForwardAreNotJudged)
{
  Game game = AtOneUp();
  for (int i = 0; i < 20; ++i)
  {
    Sample s = At(30.0, 3);
    if (i % 2)
      s.running = false;
    else
      s.speed = 1.5;
    EXPECT_EQ(game.controller.Judge(s, FLOOR, 2), 1);
  }
}

TEST(DynamicResolution, AFailedStepUpWaitsLongerEachTime)
{
  Game game;
  // 3x cannot be held, 2x always looks as if it could (the pixels say 6.75 ms).
  const auto cost = [](int scale) { return scale == 3 ? 15.0 : 3.0; };
  int ups = 0;
  double last_up = 0.0;
  std::vector<double> gaps;
  for (int second = 0; second < 4000; ++second)
  {
    if (game.Second(cost(game.scale)) == Controller::Step::Up)
    {
      if (ups++ > 0)
        gaps.push_back(second - last_up);
      last_up = second;
    }
  }
  // The measured ratio (clamped to 1.25 times the pixels': 8.4 ms) does not stop it, the waits do.
  ASSERT_EQ(ups, Controller::FAILS_FOR_GAME);
  EXPECT_GE(gaps[0], Controller::RETRY_WAIT);
  EXPECT_GE(gaps[1], Controller::RETRY_WAIT * Controller::RETRY_GROWTH);
  EXPECT_EQ(game.scale, 2);
  EXPECT_TRUE(game.controller.Blocked(3));
}

TEST(DynamicResolution, AHeldStepUpThatFailsLaterIsNotBlocked)
{
  Game game = AtOneUp();
  game.Run(Controller::FAIL_WINDOW, 8.0);
  game.Run(Controller::DOWN_AFTER, 16.0);
  EXPECT_EQ(game.scale, 2);
  EXPECT_FALSE(game.controller.Blocked(3));
}

TEST(DynamicResolution, GoodSecondsWaitAfterAStepDown)
{
  Game game = AtOneUp();
  game.Run(Controller::FAIL_WINDOW, 8.0);
  game.Run(Controller::DOWN_AFTER, 16.0);
  ASSERT_EQ(game.scale, 2);
  game.Run(Controller::SETTLE + Controller::HOLD_AFTER_DOWN + Controller::UP_AFTER - 1, 3.0);
  EXPECT_EQ(game.scale, 2);
  game.Second(3.0);
  EXPECT_EQ(game.scale, 3);
}

TEST(DynamicResolution, LearnsWhatAStepCosts)
{
  // The fixed part is large: 3x costs 1.2 times 2x, not 2.25.
  Game game;
  const auto cost = [](int scale) { return 2.0 + scale; };
  game.Run(400, cost);
  EXPECT_EQ(game.scale, 4);
  EXPECT_LT(game.controller.Ratio(2), 1.5);
  EXPECT_NEAR(game.controller.MedianCost(3), 5.0, 1e-3);
}

TEST(DynamicResolution, AStepUpsOwnMeasurementIsTheLeast)
{
  Game game;
  game.Run(Controller::UP_AFTER, 3.0);
  ASSERT_EQ(game.scale, 3);
  game.Run(Controller::SETTLE + Controller::MEASURE_SECONDS, 7.5);
  EXPECT_NEAR(game.controller.Ratio(2), 2.5, 1e-9);
  // A cheap scene at 3x does not lower it.
  game.Run(100, 3.0);
  EXPECT_GE(game.controller.Ratio(2), 2.5);
}

TEST(DynamicResolution, TheScreensCeilingIsNoStep)
{
  Game game;
  game.Run(200, 1.0);
  ASSERT_EQ(game.scale, 4);
  game.ceiling_up = 1;
  EXPECT_EQ(game.Second(1.0), Controller::Step::None);
  EXPECT_EQ(game.scale, 3);
  EXPECT_EQ(game.controller.Level(), 2);
  EXPECT_FALSE(game.controller.IsSettled());
  game.Run(100, 1.0);
  EXPECT_EQ(game.scale, 3);
  EXPECT_FALSE(game.controller.IsSettled());
  game.ceiling_up = 2;
  game.Second(1.0);
  EXPECT_EQ(game.scale, 4);
  // The setting is the floor whatever the screen shows.
  game.ceiling_up = 0;
  game.Run(100, 1.0);
  EXPECT_EQ(game.scale, 2);
}

TEST(DynamicResolution, NothingOverTheLimit)
{
  Game game;
  game.max_up = 0;
  game.Run(100, 1.0);
  EXPECT_EQ(game.scale, 2);
}

TEST(DynamicResolution, AGameStartsAtItsRememberedScale)
{
  EXPECT_EQ(StartUp(2, 0, 2), 0);
  EXPECT_EQ(StartUp(2, 3, 2), 1);
  EXPECT_EQ(StartUp(2, 4, 1), 1);
  EXPECT_EQ(StartUp(4, 3, 2), 0);

  Game game;
  game.controller.Start(1);
  game.Second(6.0);
  EXPECT_EQ(game.scale, 3);
}

TEST(DynamicResolution, AHeldScaleIsRemembered)
{
  Game game = AtOneUp();
  game.Run(Controller::REMEMBER_AFTER - 1, 8.0);
  EXPECT_FALSE(game.controller.IsSettled());
  game.Second(8.0);
  EXPECT_TRUE(game.controller.IsSettled());
  game.Run(Controller::DOWN_AFTER - 1, 16.0);
  game.Second(16.0);
  EXPECT_EQ(game.scale, 2);
  EXPECT_TRUE(game.controller.IsSettled());
}

TEST(DynamicResolution, Store)
{
  const ScaleStore store = ParseStore("GM4E01 3\r\nGFEE01 4\nRKPE01 x\n00000000 3\nSUKE01 2 bfi\n"
                                      "../evil 2\nGALE01 99\n\nID-launcher 2");
  ASSERT_EQ(store.size(), 3u);
  EXPECT_EQ(store.at("GM4E01"), 3);
  EXPECT_EQ(store.at("GFEE01"), 4);
  EXPECT_EQ(store.at("ID-launcher"), 2);
  EXPECT_EQ(FormatStore(store), "GFEE01 4\nGM4E01 3\nID-launcher 2\n");
  EXPECT_EQ(ParseStore(FormatStore(store)), store);
  EXPECT_FALSE(IsStoredGame(""));
  EXPECT_FALSE(IsStoredGame("00000000"));
}

TEST(DynamicResolution, ScreenCoverage)
{
  // A 16:9 screen 1.5 m high 2 m away, an eye of 2160 pixels over 100 degrees.
  ScreenCover cover{1.5 * 16.0 / 9.0 / 2.0, 0.75, 2.3835, 2.3835, 2160.0, 2160.0};
  EXPECT_NEAR(ScreenCoverage(cover), 4.72, 0.02);
  // Twice as near.
  cover.tan_w *= 2.0;
  cover.tan_h *= 2.0;
  EXPECT_NEAR(ScreenCoverage(cover), 9.44, 0.04);
  cover.eye_px_w = 0.0;
  EXPECT_EQ(ScreenCoverage(cover), 0.0);
}

TEST(DynamicResolution, TheCeilingIsHeld)
{
  CeilingHold ceiling;
  EXPECT_EQ(ceiling.Next(3.5), 0);
  EXPECT_EQ(ceiling.Next(3.5), 0);
  EXPECT_EQ(ceiling.Next(3.5), 3);
  // Just over the next scale is not it yet.
  for (int i = 0; i < 10; ++i)
    EXPECT_EQ(ceiling.Next(4.1), 3);
  EXPECT_EQ(ceiling.Next(4.3), 3);
  EXPECT_EQ(ceiling.Next(4.3), 3);
  EXPECT_EQ(ceiling.Next(4.3), 4);
  // Carried through many sizes.
  EXPECT_EQ(ceiling.Next(2.0), 4);
  EXPECT_EQ(ceiling.Next(6.5), 4);
  EXPECT_EQ(ceiling.Next(2.0), 4);
  EXPECT_EQ(ceiling.Next(4.5), 4);
  EXPECT_EQ(ceiling.Next(0.0), 4);
  EXPECT_EQ(ceiling.Next(0.0), 4);
  EXPECT_EQ(ceiling.Next(0.0), 0);
}

TEST(DynamicResolution, ImmersiveIsDrawnUpToTheEyeImage)
{
  EXPECT_EQ(EyeCeiling(1680, 1760), 4);
  EXPECT_EQ(EyeCeiling(2160, 2160), 5);
  // Both sides are covered, and no more than that.
  EXPECT_EQ(EyeCeiling(1920, 1584), 3);
  EXPECT_EQ(EyeCeiling(1921, 1584), 4);
  EXPECT_EQ(EyeCeiling(1920, 1585), 4);
  EXPECT_EQ(EyeCeiling(0, 0), 0);

  // Two steps up, no more than the ceiling, never under the floor.
  EXPECT_EQ(ImmersiveUp(1, 4, 2), 2);
  EXPECT_EQ(ImmersiveUp(2, 4, 2), 2);
  EXPECT_EQ(ImmersiveUp(3, 4, 2), 1);
  EXPECT_EQ(ImmersiveUp(4, 4, 2), 0);
  EXPECT_EQ(ImmersiveUp(6, 4, 2), 0);
  // What the GPU and the shaders allow.
  EXPECT_EQ(ImmersiveUp(1, 4, 0), 0);
  EXPECT_EQ(ImmersiveUp(2, 4, 1), 1);
  // Not known.
  EXPECT_EQ(ImmersiveUp(2, 0, 2), 0);
}

TEST(DynamicResolution, ImmersiveStartsAtTheTop)
{
  EXPECT_EQ(ImmersiveStartUp(2, 0, 2), 2);
  EXPECT_EQ(ImmersiveStartUp(2, 3, 2), 1);
  EXPECT_EQ(ImmersiveStartUp(2, 2, 2), 0);
  EXPECT_EQ(ImmersiveStartUp(2, 4, 1), 1);
  EXPECT_EQ(ImmersiveStartUp(2, 0, 0), 0);

  // Down to the floor with the GPU the limit, and back when there is room.
  Game game;
  game.scale = FLOOR + 2;
  game.controller.Start(ImmersiveStartUp(FLOOR, 0, game.max_up));
  game.Run(Controller::SETTLE + Controller::DOWN_AFTER - 1, 16.0);
  EXPECT_EQ(game.scale, 4);
  EXPECT_EQ(game.Second(16.0), Controller::Step::Down);
  EXPECT_EQ(game.scale, 3);
  game.Run(Controller::SETTLE + Controller::DOWN_AFTER, 16.0);
  EXPECT_EQ(game.scale, 2);
  game.Run(100, 16.0);
  EXPECT_EQ(game.scale, 2);
  game.Run(100, [](int scale) { return 0.4 * scale * scale; });
  EXPECT_EQ(game.scale, 4);
}

TEST(DynamicResolution, ImmersiveHasItsOwnPlaceInTheStore)
{
  EXPECT_EQ(StoreKey("GM4E01", false), "GM4E01");
  EXPECT_EQ(StoreKey("GM4E01", true), "GM4E01-immersive");
  EXPECT_EQ(StoreKey("00000000", true), "");
  EXPECT_EQ(StoreKey("", true), "");
  const ScaleStore store{{StoreKey("GM4E01", true), 4}, {StoreKey("GM4E01", false), 3}};
  EXPECT_EQ(ParseStore(FormatStore(store)), store);
}
